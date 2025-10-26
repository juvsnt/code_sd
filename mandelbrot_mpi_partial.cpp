// mandelbrot_mpi_partial.cpp
// Compile: mpicxx -O3 -std=c++17 -fopenmp -o mandelbrot_mpi_partial mandelbrot_mpi_partial.cpp

#include <mpi.h>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>    // std::rename
#include <algorithm>
#ifdef _OPENMP
#include <omp.h>
#endif

enum Tags { TAG_TASK = 1, TAG_RESULT = 2, TAG_STOP = 3 };

struct Args {
    int width = 1920;
    int height = 1080;
    int maxiter = 1000;
    int tilesize = 64;
    std::string outfile = "mandelbrot.ppm";
    int snapshot_interval = 10; // save every N tiles
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string s = argv[i];
        if (s == "-w" && i+1<argc) a.width = std::stoi(argv[++i]);
        else if (s == "-h" && i+1<argc) a.height = std::stoi(argv[++i]);
        else if (s == "-iter" && i+1<argc) a.maxiter = std::stoi(argv[++i]);
        else if (s == "-tilesize" && i+1<argc) a.tilesize = std::stoi(argv[++i]);
        else if (s == "-outfile" && i+1<argc) a.outfile = argv[++i];
        else if (s == "-snapshot" && i+1<argc) a.snapshot_interval = std::max(1, std::stoi(argv[++i]));
    }
    return a;
}

void iter_to_rgb(int iter, int maxiter, uint8_t &r, uint8_t &g, uint8_t &b) {
    if (iter >= maxiter) { r = g = b = 0; return; }
    double t = double(iter) / double(maxiter);
    r = uint8_t(std::clamp(9*(1-t)*t*t*t*255.0, 0.0, 255.0));
    g = uint8_t(std::clamp(15*(1-t)*(1-t)*t*t*255.0, 0.0, 255.0));
    b = uint8_t(std::clamp(8.5*(1-t)*(1-t)*(1-t)*t*255.0, 0.0, 255.0));
}

void compute_tile(int image_w, int image_h, int maxiter,
                  int x0, int y0, int tw, int th,
                  double x_min, double x_max, double y_min, double y_max,
                  std::vector<uint8_t> &buffer)
{
    buffer.resize(tw * th * 3);
    double dx = (x_max - x_min) / (image_w - 1);
    double dy = (y_max - y_min) / (image_h - 1);

    #pragma omp parallel for schedule(dynamic)
    for (int j = 0; j < th; ++j) {
        for (int i = 0; i < tw; ++i) {
            int px = x0 + i;
            int py = y0 + j;
            uint8_t r,g,b;
            if (px >= image_w || py >= image_h) {
                r = g = b = 0;
            } else {
                double cx = x_min + px * dx;
                double cy = y_max - py * dy;
                double zx = 0.0, zy = 0.0;
                int iter = 0;
                double zx2 = 0.0, zy2 = 0.0;
                while (zx2 + zy2 <= 4.0 && iter < maxiter) {
                    zy = 2.0*zx*zy + cy;
                    zx = zx2 - zy2 + cx;
                    zx2 = zx*zx;
                    zy2 = zy*zy;
                    ++iter;
                }
                iter_to_rgb(iter, maxiter, r, g, b);
            }
            int idx = (j * tw + i) * 3;
            buffer[idx+0] = r;
            buffer[idx+1] = g;
            buffer[idx+2] = b;
        }
    }
}

bool save_ppm_atomic(const std::string &outname, const std::vector<uint8_t> &image, int w, int h) {
    std::string tmp = outname + ".tmp";
    std::ofstream ofs(tmp, std::ios::binary);
    if (!ofs) return false;
    ofs << "P6\n" << w << " " << h << "\n255\n";
    ofs.write(reinterpret_cast<const char*>(image.data()), image.size());
    ofs.close();
    // rename tmp -> outname (overwrite if exists)
    if (std::rename(tmp.c_str(), outname.c_str()) != 0) {
        // rename failed
        std::perror("rename");
        return false;
    }
    return true;
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    Args args = parse_args(argc, argv);
    const int master = 0;

    const double x_min = -2.5, x_max = 1.0;
    const double y_min = -1.2, y_max = 1.2;

    if (rank == master) {
        int image_w = args.width, image_h = args.height;
        int tile = args.tilesize;
        int maxiter = args.maxiter;
        struct Tile { int x0,y0,w,h; };
        std::vector<Tile> tiles;
        for (int y = 0; y < image_h; y += tile) {
            for (int x = 0; x < image_w; x += tile) {
                int tw = std::min(tile, image_w - x);
                int th = std::min(tile, image_h - y);
                tiles.push_back({x,y,tw,th});
            }
        }
        int total_tiles = (int)tiles.size();
        std::vector<uint8_t> image(image_w * image_h * 3, 0);

        std::cout << "IMAGE " << image_w << "x" << image_h << " tilesize=" << tile
                  << " tiles=" << total_tiles << " maxiter=" << maxiter << "\n";

        double t_start = MPI_Wtime();
        int next_tile = 0;
        int workers = std::max(1, size - 1);

        // send initial tasks
        for (int dest = 1; dest <= workers && next_tile < total_tiles; ++dest) {
            Tile &T = tiles[next_tile++];
            int header[4] = {T.x0, T.y0, T.w, T.h};
            MPI_Send(header, 4, MPI_INT, dest, TAG_TASK, MPI_COMM_WORLD);
        }

        int finished_tiles = 0;
        while (finished_tiles < total_tiles) {
            MPI_Status status;
            int header[4];
            MPI_Recv(header, 4, MPI_INT, MPI_ANY_SOURCE, TAG_RESULT, MPI_COMM_WORLD, &status);
            int src = status.MPI_SOURCE;
            int x0 = header[0], y0 = header[1], tw = header[2], th = header[3];
            int bytes = tw * th * 3;
            std::vector<uint8_t> buf(bytes);
            MPI_Recv(buf.data(), bytes, MPI_UNSIGNED_CHAR, src, TAG_RESULT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            // copy into final image buffer
            for (int row = 0; row < th; ++row) {
                int dest_row = y0 + row;
                for (int col = 0; col < tw; ++col) {
                    int dest_col = x0 + col;
                    int dst_idx = (dest_row * image_w + dest_col) * 3;
                    int src_idx = (row * tw + col) * 3;
                    image[dst_idx+0] = buf[src_idx+0];
                    image[dst_idx+1] = buf[src_idx+1];
                    image[dst_idx+2] = buf[src_idx+2];
                }
            }

            ++finished_tiles;
            // progress info
            double pct = 100.0 * finished_tiles / total_tiles;
            std::cout << "\rTiles: " << finished_tiles << "/" << total_tiles
                      << " (" << int(pct) << "%) " << std::flush;

            // snapshot save logic
            if ( (finished_tiles % args.snapshot_interval) == 0 || finished_tiles == total_tiles) {
                bool ok = save_ppm_atomic(args.outfile, image, image_w, image_h);
                if (!ok) std::cerr << "\nWARNING: couldn't write snapshot " << args.outfile << "\n";
            }

            // send next or stop
            if (next_tile < total_tiles) {
                Tile &T = tiles[next_tile++];
                int header2[4] = {T.x0, T.y0, T.w, T.h};
                MPI_Send(header2, 4, MPI_INT, src, TAG_TASK, MPI_COMM_WORLD);
            } else {
                MPI_Send(nullptr, 0, MPI_INT, src, TAG_STOP, MPI_COMM_WORLD);
            }
        }

        double t_end = MPI_Wtime();
        std::cout << "\nTotal render time (s): " << (t_end - t_start) << "\n";
        // final save ensured above but save once more to be safe
        save_ppm_atomic(args.outfile, image, image_w, image_h);
        std::cout << "Saved " << args.outfile << "\n";

    } else {
        int image_w = args.width, image_h = args.height;
        int maxiter = args.maxiter;
        while (true) {
            MPI_Status status;
            MPI_Probe(master, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
            if (status.MPI_TAG == TAG_TASK) {
                int header[4];
                MPI_Recv(header, 4, MPI_INT, master, TAG_TASK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                int x0 = header[0], y0 = header[1], tw = header[2], th = header[3];
                std::vector<uint8_t> buf;
                compute_tile(image_w, image_h, maxiter, x0, y0, tw, th, x_min, x_max, y_min, y_max, buf);
                int out_header[4] = {x0, y0, tw, th};
                MPI_Send(out_header, 4, MPI_INT, master, TAG_RESULT, MPI_COMM_WORLD);
                MPI_Send(buf.data(), (int)buf.size(), MPI_UNSIGNED_CHAR, master, TAG_RESULT, MPI_COMM_WORLD);
            } else if (status.MPI_TAG == TAG_STOP) {
                MPI_Recv(nullptr, 0, MPI_INT, master, TAG_STOP, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                break;
            } else {
                MPI_Recv(nullptr, 0, MPI_INT, master, status.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
        }
    }

    MPI_Finalize();
    return 0;
}

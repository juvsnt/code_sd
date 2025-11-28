// mpi_pi_montecarlo.cpp
// Calculo de Pi por Monte Carlo com MPI (master/worker) + OpenMP opcional
// Compile: mpicxx -O3 -std=c++17 -fopenmp -o mpi_pi_montecarlo mpi_pi_montecarlo.cpp

#include <mpi.h>
#include <cstdint>
#include <random>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>

#ifdef _OPENMP
#include <omp.h>
#endif

enum Tags { TAG_TASK = 1, TAG_RESULT = 2, TAG_STOP = 3 };

struct Args {
    uint64_t samples_total = 1000000ULL; // 1e6
    uint64_t batch = 1000000ULL;         // 1e6 por tarefa
    int report_every = 10;                // imprime parcial a cada N tarefas
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i=1;i<argc;++i) {
        std::string s = argv[i];
        if (s=="-samples" && i+1<argc) a.samples_total = std::stoull(argv[++i]);
        else if (s=="-batch" && i+1<argc) a.batch = std::stoull(argv[++i]);
        else if (s=="-report" && i+1<argc) a.report_every = std::max(1, std::stoi(argv[++i]));
    }
    if (a.batch == 0) a.batch = 1000000ULL;
    if (a.batch > a.samples_total) a.batch = a.samples_total;
    return a;
}

// Gera 'n' pontos uniformes em [-1,1]x[-1,1] e retorna quantos caem no círculo de raio 1
static inline uint64_t hits_in_circle(uint64_t n, uint64_t seed_base) {
    uint64_t hits = 0ULL;

#ifdef _OPENMP
    int max_threads = omp_get_max_threads();
    #pragma omp parallel reduction(+:hits)
    {
        int tid = 0;
        #ifdef _OPENMP
        tid = omp_get_thread_num();
        #endif
        // semente por thread para independência dos fluxos
        std::mt19937_64 rng(seed_base ^ (0x9E3779B97F4A7C15ULL * (tid+1)));
        std::uniform_real_distribution<double> dist(-1.0, 1.0);

        // dividir n entre as threads (quase) igualmente
        uint64_t base = n / max_threads;
        uint64_t extra = n % max_threads;
        uint64_t my_n = base + (uint64_t)(tid < extra ? 1 : 0);

        uint64_t local = 0ULL;
        for (uint64_t i=0; i<my_n; ++i) {
            double x = dist(rng);
            double y = dist(rng);
            if (x*x + y*y <= 1.0) ++local;
        }
        hits += local;
    }
#else
    std::mt19937_64 rng(seed_base);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    for (uint64_t i=0; i<n; ++i) {
        double x = dist(rng);
        double y = dist(rng);
        if (x*x + y*y <= 1.0) ++hits;
    }
#endif

    return hits;
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank=0, size=1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    const int MASTER = 0;
    Args args = parse_args(argc, argv);

    // Timer global
    double t0 = 0, t1 = 0;
    if (rank == MASTER) t0 = MPI_Wtime();

    // MASTER: cria fila de tarefas (cada tarefa = 'batch' amostras)
    if (rank == MASTER) {
        // Quantidade de tarefas e possível resto
        uint64_t tasks = args.samples_total / args.batch;
        uint64_t remainder = args.samples_total % args.batch;
        if (remainder > 0) ++tasks; // última tarefa tem menos pontos

        if (size < 2) {
            std::cerr << "[Aviso] Rodando com 1 processo: cálculo local. Para demonstrar distribuição, use -np >= 2.\n";
        }
        std::cout << "Monte Carlo Pi (MPI"
#ifdef _OPENMP
                  << "+OpenMP"
#endif
                  << ") | total samples=" << args.samples_total
                  << " | batch=" << args.batch
                  << " | tasks=" << tasks
                  << " | workers=" << std::max(0, size-1)
                  << "\n";

        uint64_t total_hits = 0ULL;
        uint64_t total_done_samples = 0ULL;

        // Distribuição inicial para até 'size-1' workers
        uint64_t next_task_id = 0; // também usado para semente distinta
        int workers = std::max(0, size-1);

        // Envia tarefas iniciais
        for (int w = 1; w <= workers && next_task_id < tasks; ++w) {
            uint64_t n_for_task = (next_task_id == tasks-1 && remainder>0) ? remainder : args.batch;
            uint64_t payload[2] = { n_for_task, next_task_id }; // [0]=amostras, [1]=id p/ semente
            MPI_Send(payload, 2, MPI_UNSIGNED_LONG_LONG, w, TAG_TASK, MPI_COMM_WORLD);
            ++next_task_id;
        }

        uint64_t received_tasks = 0;
        while (received_tasks < tasks) {
            // Recebe resultado de qualquer worker
            uint64_t result[2]; // [0] hits, [1] samples_processadas
            MPI_Status st;
            MPI_Recv(result, 2, MPI_UNSIGNED_LONG_LONG, MPI_ANY_SOURCE, TAG_RESULT, MPI_COMM_WORLD, &st);
            int src = st.MPI_SOURCE;

            total_hits += result[0];
            total_done_samples += result[1];
            ++received_tasks;

            // Relatório parcial
            if (args.report_every > 0 && (received_tasks % args.report_every == 0 || received_tasks == tasks)) {
                double pi_est = 4.0 * (double) total_hits / (double) total_done_samples;
                double pct = 100.0 * (double) received_tasks / (double) tasks;
                std::cout << "\r[Master] tasks " << received_tasks << "/" << tasks
                          << " (" << (int)pct << "%) | samples=" << total_done_samples
                          << " | pi~=" << pi_est << std::flush;
            }

            // Envia próxima tarefa para quem ficou livre, ou STOP se acabou
            if (next_task_id < tasks) {
                uint64_t n_for_task = (next_task_id == tasks-1 && remainder>0) ? remainder : args.batch;
                uint64_t payload[2] = { n_for_task, next_task_id };
                MPI_Send(payload, 2, MPI_UNSIGNED_LONG_LONG, src, TAG_TASK, MPI_COMM_WORLD);
                ++next_task_id;
            } else {
                MPI_Send(nullptr, 0, MPI_UNSIGNED_LONG_LONG, src, TAG_STOP, MPI_COMM_WORLD);
            }
        }

        t1 = MPI_Wtime();
        double pi_est = 4.0 * (double) total_hits / (double) total_done_samples;
        std::cout << "\nPi estimado = " << pi_est
                  << " | amostras=" << total_done_samples
                  << " | tempo=" << (t1 - t0) << " s\n";

    } else {
        // WORKER: recebe tarefas até receber STOP
        while (true) {
            MPI_Status st;
            // Espia a próxima mensagem para checar TAG
            MPI_Probe(MASTER, MPI_ANY_TAG, MPI_COMM_WORLD, &st);
            if (st.MPI_TAG == TAG_TASK) {
                uint64_t payload[2];
                MPI_Recv(payload, 2, MPI_UNSIGNED_LONG_LONG, MASTER, TAG_TASK, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                uint64_t n = payload[0];
                uint64_t task_id = payload[1];

                // Semente: combina task_id com rank para fluxos distintos
                uint64_t seed = 0xA5A5A5A55A5A5A5AULL ^ (uint64_t)rank ^ (task_id * 0x9E3779B97F4A7C15ULL);

                uint64_t h = hits_in_circle(n, seed);

                uint64_t result[2] = { h, n };
                MPI_Send(result, 2, MPI_UNSIGNED_LONG_LONG, MASTER, TAG_RESULT, MPI_COMM_WORLD);

            } else if (st.MPI_TAG == TAG_STOP) {
                MPI_Recv(nullptr, 0, MPI_UNSIGNED_LONG_LONG, MASTER, TAG_STOP, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                break;
            } else {
                // descarta qualquer coisa inesperada
                MPI_Recv(nullptr, 0, MPI_UNSIGNED_LONG_LONG, MASTER, st.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
        }
    }

    MPI_Finalize();
    return 0;
}

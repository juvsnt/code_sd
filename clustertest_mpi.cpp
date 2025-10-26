#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <mpi.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);

    int num_tasks, task_id;
    MPI_Comm_size(MPI_COMM_WORLD, &num_tasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &task_id);

    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    // --- Início da medição de tempo ---
    double start_time = MPI_Wtime();

    // Simulação de trabalho (pode ser substituída pelo código real)
    usleep(100000 * (task_id + 1)); // Exemplo: cada nó espera tempo diferente

    double end_time = MPI_Wtime();
    double elapsed = end_time - start_time;

    std::stringstream ss;
    ss << "Task " << task_id << "/" << num_tasks 
       << " running on " << hostname 
       << " | Time: " << elapsed << " s\n";
    std::cout << ss.str();

    // --- Coleta todos os tempos no nó 0 ---
    std::vector<double> all_times(num_tasks);
    MPI_Gather(&elapsed, 1, MPI_DOUBLE, all_times.data(), 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    if (task_id == 0) {
        double max_time = 0.0;
        double sum_time = 0.0;

        for (int i = 0; i < num_tasks; ++i) {
            sum_time += all_times[i];
            if (all_times[i] > max_time)
                max_time = all_times[i];
        }

        double speedup = max_time / max_time; // Speedup em relação ao maior tempo do nó (sequencial)
        double efficiency = speedup / num_tasks;

        // --- Grava resultados em CSV ---
        std::ofstream csv_file("mpi_results.csv");
        csv_file << "Task,Hostname,Time(s)\n";
        for (int i = 0; i < num_tasks; ++i) {
            csv_file << i << "," << hostname << "," << all_times[i] << "\n";
        }
        csv_file << "\nMaxTime(s)," << max_time << "\n";
        csv_file << "Speedup," << speedup << "\n";
        csv_file << "Efficiency," << efficiency << "\n";
        csv_file.close();

        std::cout << "\n=== Resumo ===\n";
        std::cout << "Max Time: " << max_time << " s\n";
        std::cout << "Speedup: " << speedup << "\n";
        std::cout << "Efficiency: " << efficiency << "\n";
        std::cout << "Resultados salvos em mpi_results.csv\n";
    }

    MPI_Finalize();
    return 0;
}

#include <mpi.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <unistd.h>
#include <cstdlib>

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);

    int num_tasks, task_id;
    MPI_Comm_size(MPI_COMM_WORLD, &num_tasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &task_id);

    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    // Total de veículos na simulação
    long long total_vehicles = 125000000; // 125 milhões
    int timesteps = 500;                  // número de etapas

    // --- Balanceamento simples ---
    // Distribui veículos entre os nós quase igualmente
    long long base_vehicles_per_node = total_vehicles / num_tasks;
    long long remainder = total_vehicles % num_tasks;
    long long vehicles_to_process = base_vehicles_per_node + (task_id < remainder ? 1 : 0);

    // --- Início do tempo ---
    double start_time = MPI_Wtime();

    // --- Simulação do trânsito ---
    long long vehicles_arrived = 0;
    for (int t = 0; t < timesteps; ++t) {
        for (long long v = 0; v < vehicles_to_process / timesteps; ++v) {
            if (rand() % 2 == 0) vehicles_arrived++;
        }
        usleep(50); // simula processamento
    }

    double end_time = MPI_Wtime();
    double elapsed = end_time - start_time;

    std::cout << "Node " << task_id << " on " << hostname
              << " | Time: " << elapsed
              << " s | Vehicles arrived: " << vehicles_arrived << "\n";

    // --- Coleta todos os resultados ---
    std::vector<long long> all_vehicles(num_tasks);
    std::vector<double> all_times(num_tasks);

    MPI_Gather(&vehicles_arrived, 1, MPI_LONG_LONG, all_vehicles.data(), 1, MPI_LONG_LONG, 0, MPI_COMM_WORLD);
    MPI_Gather(&elapsed, 1, MPI_DOUBLE, all_times.data(), 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    if (task_id == 0) {
        long long total_arrived = 0;
        double max_time = 0.0;
        double sum_time = 0.0;

        for (int i = 0; i < num_tasks; ++i) {
            total_arrived += all_vehicles[i];
            if (all_times[i] > max_time) max_time = all_times[i];
            sum_time += all_times[i];
        }

        // Speedup relativo ao nó mais rápido (ou ao tempo sequencial se quiser)
        double speedup = all_times[0] / max_time; // nó 0 como referência
        double efficiency = speedup / num_tasks;

        // --- Grava CSV ---
        std::ofstream csv_file("traffic_simulation_balanced.csv");
        csv_file << "Node,Hostname,VehiclesArrived,Time(s)\n";
        for (int i = 0; i < num_tasks; ++i) {
            csv_file << i << "," << hostname << "," << all_vehicles[i] << "," << all_times[i] << "\n";
        }
        csv_file << "\nTotalVehicles," << total_arrived << "\n";
        csv_file << "MaxTime(s)," << max_time << "\n";
        csv_file << "Speedup," << speedup << "\n";
        csv_file << "Efficiency," << efficiency << "\n";
        csv_file.close();

        std::cout << "\n=== Summary ===\n";
        std::cout << "Total vehicles arrived: " << total_arrived << "\n";
        std::cout << "Max Time: " << max_time << " s\n";
        std::cout << "Speedup: " << speedup << "\n";
        std::cout << "Efficiency: " << efficiency << "\n";
        std::cout << "Results saved in traffic_simulation_balanced.csv\n";
    }

    MPI_Finalize();
    return 0;
}

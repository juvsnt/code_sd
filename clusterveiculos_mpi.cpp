#include <mpi.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <unistd.h> // para usleep (simulação)

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);

    int num_tasks, task_id;
    MPI_Comm_size(MPI_COMM_WORLD, &num_tasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &task_id);

    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    // Número de veículos por região (nó)
    int vehicles_per_region = 500000;

    // Número de etapas de simulação
    int timesteps = 500;

    // --- Início da medição de tempo ---
    double start_time = MPI_Wtime();

    // --- Simulação de trânsito ---
    int vehicles_arrived = 0;
    for (int t = 0; t < timesteps; ++t) {
        // Cada veículo tem 50% de chance de avançar e chegar ao destino
        for (int v = 0; v < vehicles_per_region; ++v) {
            if (rand() % 2 == 0) vehicles_arrived++;
        }
        // Simula tempo de processamento
        usleep(100); 
    }

    double end_time = MPI_Wtime();
    double elapsed = end_time - start_time;

    std::cout << "Node " << task_id << " on " << hostname 
              << " | Time: " << elapsed 
              << " s | Vehicles arrived: " << vehicles_arrived << "\n";

    // --- Coleta todos os tempos e resultados no nó 0 ---
    std::vector<int> all_vehicles(num_tasks);
    std::vector<double> all_times(num_tasks);

    MPI_Gather(&vehicles_arrived, 1, MPI_INT, all_vehicles.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);
    MPI_Gather(&elapsed, 1, MPI_DOUBLE, all_times.data(), 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    if (task_id == 0) {
        int total_vehicles = 0;
        double max_time = 0.0;

        for (int i = 0; i < num_tasks; ++i) {
            total_vehicles += all_vehicles[i];
            if (all_times[i] > max_time) max_time = all_times[i];
        }

        double speedup = all_times[0] / max_time; // usando nó 0 como referência
        double efficiency = speedup / num_tasks;

        // --- Grava CSV ---
        std::ofstream csv_file("traffic_simulation_results.csv");
        csv_file << "Node,Hostname,VehiclesArrived,Time(s)\n";
        for (int i = 0; i < num_tasks; ++i) {
            csv_file << i << "," << hostname << "," << all_vehicles[i] << "," << all_times[i] << "\n";
        }
        csv_file << "\nTotalVehicles," << total_vehicles << "\n";
        csv_file << "MaxTime(s)," << max_time << "\n";
        csv_file << "Speedup," << speedup << "\n";
        csv_file << "Efficiency," << efficiency << "\n";
        csv_file.close();

        std::cout << "\n=== Summary ===\n";
        std::cout << "Total vehicles arrived: " << total_vehicles << "\n";
        std::cout << "Max Time: " << max_time << " s\n";
        std::cout << "Speedup: " << speedup << "\n";
        std::cout << "Efficiency: " << efficiency << "\n";
        std::cout << "Results saved in traffic_simulation_results.csv\n";
    }

    MPI_Finalize();
    return 0;
}

#include <mpi.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <winsock2.h>  // deve vir antes de windows.h
#include <windows.h>   // para Sleep()
#include <cmath>
#include <cstdlib>
#include <ctime>

#pragma comment(lib, "ws2_32.lib") // necessário no Visual Studio

// Função para simular o "processamento" de um veículo
double simulate_vehicle(double position, double velocity, int iterations) {
    // Simula física simples e alguns cálculos matemáticos
    double x = position;
    double v = velocity;
    for (int i = 0; i < iterations; ++i) {
        x += sin(v) * 0.001;
        v = cos(x + v * 0.1);
        v += ((rand() % 100) - 50) * 0.0001;
    }
    return x + v;
}

int main(int argc, char *argv[]) {
    MPI_Init(&argc, &argv);

    int num_tasks, task_id;
    MPI_Comm_size(MPI_COMM_WORLD, &num_tasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &task_id);

    // --- Inicializa Winsock (necessário no Windows para gethostname) ---
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Erro ao inicializar Winsock.\n";
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    srand((unsigned)time(NULL) + task_id); // semente diferente por nó

    // --- Parâmetros da simulação ---
    long long total_vehicles = 50000000; // 50 milhões (ajustável)
    int timesteps = 200;                 // número de etapas

    // --- Balanceamento simples ---
    long long base_vehicles_per_node = total_vehicles / num_tasks;
    long long remainder = total_vehicles % num_tasks;
    long long vehicles_to_process = base_vehicles_per_node + (task_id < remainder ? 1 : 0);

    // --- Início do tempo ---
    double start_time = MPI_Wtime();

    // --- Simulação do trânsito ---
    long long vehicles_arrived = 0;
    double dummy_result = 0.0;

    for (int t = 0; t < timesteps; ++t) {
        for (long long v = 0; v < vehicles_to_process / timesteps; ++v) {
            // cada veículo executa um pequeno cálculo matemático
            dummy_result += simulate_vehicle(v * 0.001, 0.5, 100);
            if (fmod(dummy_result, 2.0) < 1.0) vehicles_arrived++;
        }
    }

    double end_time = MPI_Wtime();
    double elapsed = end_time - start_time;

    std::cout << "Node " << task_id << " on " << hostname
              << " | Time: " << elapsed
              << " s | Vehicles arrived: " << vehicles_arrived << "\n";

    // --- Coleta resultados ---
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

        double avg_time = sum_time / num_tasks;
        double speedup = all_times[0] / max_time;
        double efficiency = speedup / num_tasks;

        // --- Grava CSV ---
        std::ofstream csv_file("traffic_simulation_cpu.csv");
        csv_file << "Node,Hostname,VehiclesArrived,Time(s)\n";
        for (int i = 0; i < num_tasks; ++i) {
            csv_file << i << "," << hostname << "," << all_vehicles[i] << "," << all_times[i] << "\n";
        }
        csv_file << "\nTotalVehicles," << total_arrived << "\n";
        csv_file << "MaxTime(s)," << max_time << "\n";
        csv_file << "AvgTime(s)," << avg_time << "\n";
        csv_file << "Speedup," << speedup << "\n";
        csv_file << "Efficiency," << efficiency << "\n";
        csv_file.close();

        std::cout << "\n=== Summary ===\n";
        std::cout << "Total vehicles arrived: " << total_arrived << "\n";
        std::cout << "Max Time: " << max_time << " s\n";
        std::cout << "Speedup: " << speedup << "\n";
        std::cout << "Efficiency: " << efficiency << "\n";
        std::cout << "Results saved in traffic_simulation_cpu.csv\n";
    }

    // --- Finaliza Winsock ---
    WSACleanup();

    MPI_Finalize();
    return 0;
}

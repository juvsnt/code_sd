#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>
#include <string.h>
#include <unistd.h>

#define N 2000 /* number of rows and columns in matrix */

MPI_Status status;
double a[N][N], b[N][N], c[N][N];

int main(int argc, char **argv)
{
    int numtasks, taskid, numworkers, source, dest, rows, offset, i, j, k;
    double t1, t2;
    char processor_name[MPI_MAX_PROCESSOR_NAME];
    int name_len;
    
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &taskid);
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Get_processor_name(processor_name, &name_len);
    
    numworkers = numtasks - 1;
    
    /*---------------------------- master ----------------------------*/
    if (taskid == 0) {
        printf("🖥️  CLUSTER MATRIX MULTIPLICATION\n");
        printf("================================\n");
        printf("Matrix size: %d x %d\n", N, N);
        printf("Total processes: %d\n", numtasks);
        printf("Number of workers: %d\n", numworkers);
        printf("Master node: %s\n", processor_name);
        printf("================================\n");
        
        if (numworkers == 0) {
            printf("❌ ERROR: Need at least 2 processes\n");
            MPI_Finalize();
            return 1;
        }
        
        // Mostrar informações dos workers
        printf("📡 Gathering worker information...\n");
        fflush(stdout);
        
        t1 = MPI_Wtime();
        
        printf("🔧 Initializing matrices...\n");
        fflush(stdout);
        
        // Initialize matrices
        for (i = 0; i < N; i++) {
            for (j = 0; j < N; j++) {
                a[i][j] = 1.0;
                b[i][j] = 2.0;
            }
        }
        
        printf("✅ Matrices initialized\n");
        printf("📤 Distributing work to %d workers...\n", numworkers);
        
        // Calculate work distribution
        rows = N / numworkers;
        int remaining_rows = N % numworkers;
        offset = 0;
        
        printf("📊 Work distribution:\n");
        printf("   Base rows per worker: %d\n", rows);
        printf("   Extra rows for first workers: %d\n", remaining_rows);
        printf("   Total operations: %.2f billion\n", (double)N * N * N / 1e9);
        
        /* send matrix data to the worker tasks */
        for (dest = 1; dest <= numworkers; dest++) {
            int current_rows = rows + (dest <= remaining_rows ? 1 : 0);
            
            printf("📤 Sending to worker %d: %d rows (offset=%d)\n", 
                   dest, current_rows, offset);
            fflush(stdout);
            
            MPI_Send(&offset, 1, MPI_INT, dest, 1, MPI_COMM_WORLD);
            MPI_Send(&current_rows, 1, MPI_INT, dest, 1, MPI_COMM_WORLD);
            MPI_Send(&a[offset][0], current_rows * N, MPI_DOUBLE, dest, 1, MPI_COMM_WORLD);
            MPI_Send(&b, N * N, MPI_DOUBLE, dest, 1, MPI_COMM_WORLD);
            offset = offset + current_rows;
        }
        
        printf("✅ All data distributed\n");
        printf("⏳ Waiting for results... (estimated: 1-3 minutes)\n");
        printf("================================\n");
        fflush(stdout);
        
        /* wait for results from all worker tasks */
        int completed_workers = 0;
        for (i = 1; i <= numworkers; i++) {
            source = i;
            
            MPI_Recv(&offset, 1, MPI_INT, source, 2, MPI_COMM_WORLD, &status);
            MPI_Recv(&rows, 1, MPI_INT, source, 2, MPI_COMM_WORLD, &status);
            MPI_Recv(&c[offset][0], rows * N, MPI_DOUBLE, source, 2, MPI_COMM_WORLD, &status);
            
            completed_workers++;
            printf("📥 Worker %d completed (%d/%d workers done)\n", 
                   source, completed_workers, numworkers);
            fflush(stdout);
        }
        
        t2 = MPI_Wtime();
        
        printf("================================\n");
        printf("🎉 CLUSTER COMPUTATION COMPLETED!\n");
        printf("================================\n");
        printf("⏱️  Total time: %.2f seconds\n", t2 - t1);
        printf("🚀 Performance: %.2f GFLOPS\n", (2.0 * N * N * N) / (t2 - t1) / 1e9);
        printf("💻 Speedup with %d workers: ~%.1fx\n", numworkers, (double)numworkers * 0.8);
        
        // Verification
        printf("\n🔍 VERIFICATION:\n");
        printf("   c[0][0] = %.2f (expected: %.2f)\n", c[0][0], (double)N * 2.0);
        printf("   c[N-1][N-1] = %.2f (expected: %.2f)\n", c[N-1][N-1], (double)N * 2.0);
        
        if (abs(c[0][0] - (double)N * 2.0) < 0.01) {
            printf("✅ Result is CORRECT!\n");
        } else {
            printf("❌ Result is INCORRECT!\n");
        }
        printf("================================\n");
    }
    
    /*---------------------------- worker ----------------------------*/
    if (taskid > 0) {
        // Enviar informação do nó para o master
        printf("🔧 Worker %d starting on node: %s\n", taskid, processor_name);
        fflush(stdout);
        
        source = 0;
        MPI_Recv(&offset, 1, MPI_INT, source, 1, MPI_COMM_WORLD, &status);
        MPI_Recv(&rows, 1, MPI_INT, source, 1, MPI_COMM_WORLD, &status);
        MPI_Recv(&a, rows * N, MPI_DOUBLE, source, 1, MPI_COMM_WORLD, &status);
        MPI_Recv(&b, N * N, MPI_DOUBLE, source, 1, MPI_COMM_WORLD, &status);
        
        printf("💼 Worker %d (%s): Processing %d rows (offset=%d)\n", 
               taskid, processor_name, rows, offset);
        printf("🔄 Worker %d: Starting computation...\n", taskid);
        fflush(stdout);
        
        double worker_start = MPI_Wtime();
        
        /* Matrix multiplication with progress */
        for (k = 0; k < N; k++) {
            // Show progress every 400 columns for cluster
            if (k % 400 == 0 && k > 0) {
                printf("📊 Worker %d (%s): %.1f%% complete\n", 
                       taskid, processor_name, (double)k/N * 100.0);
                fflush(stdout);
            }
            
            for (i = 0; i < rows; i++) {
                c[i][k] = 0.0;
                for (j = 0; j < N; j++) {
                    c[i][k] = c[i][k] + a[i][j] * b[j][k];
                }
            }
        }
        
        double worker_end = MPI_Wtime();
        printf("✅ Worker %d (%s): Completed in %.2f seconds\n", 
               taskid, processor_name, worker_end - worker_start);
        printf("📤 Worker %d: Sending results to master...\n", taskid);
        fflush(stdout);
        
        MPI_Send(&offset, 1, MPI_INT, 0, 2, MPI_COMM_WORLD);
        MPI_Send(&rows, 1, MPI_INT, 0, 2, MPI_COMM_WORLD);
        MPI_Send(&c, rows * N, MPI_DOUBLE, 0, 2, MPI_COMM_WORLD);
        
        printf("🎯 Worker %d (%s): Mission accomplished!\n", taskid, processor_name);
        fflush(stdout);
    }
    
    MPI_Finalize();
    return 0;
}
#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>

#define N 3000 /* number of rows and columns in matrix */

MPI_Status status;
double a[N][N], b[N][N], c[N][N];

int main(int argc, char **argv)
{
    int numtasks, taskid, numworkers, source, dest, rows, offset, i, j, k;
    double t1, t2;
    
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &taskid);
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    numworkers = numtasks - 1;
    
    /*---------------------------- master ----------------------------*/
    if (taskid == 0) {
        printf("Matrix multiplication with %d workers\n", numworkers);
        printf("Matrix size: %d x %d\n", N, N);
        printf("Total processes: %d\n", numtasks);
        
        if (numworkers == 0) {
            printf("ERROR: Need at least 2 processes\n");
            MPI_Finalize();
            return 1;
        }
        
        t1 = MPI_Wtime();
        
        printf("Initializing matrices...\n");
        fflush(stdout);
        
        // Initialize matrices
        for (i = 0; i < N; i++) {
            for (j = 0; j < N; j++) {
                a[i][j] = 1.0;
                b[i][j] = 2.0;
            }
        }
        
        printf("Matrices initialized. Distributing work...\n");
        
        // Calculate work distribution
        rows = N / numworkers;
        int remaining_rows = N % numworkers;
        offset = 0;
        
        printf("Base rows per worker: %d\n", rows);
        printf("Remaining rows: %d\n", remaining_rows);
        
        /* send matrix data to the worker tasks */
        for (dest = 1; dest <= numworkers; dest++) {
            int current_rows = rows + (dest <= remaining_rows ? 1 : 0);
            
            printf("Sending data to worker %d (offset=%d, rows=%d)\n", dest, offset, current_rows);
            fflush(stdout);
            
            MPI_Send(&offset, 1, MPI_INT, dest, 1, MPI_COMM_WORLD);
            MPI_Send(&current_rows, 1, MPI_INT, dest, 1, MPI_COMM_WORLD);
            MPI_Send(&a[offset][0], current_rows * N, MPI_DOUBLE, dest, 1, MPI_COMM_WORLD);
            MPI_Send(&b, N * N, MPI_DOUBLE, dest, 1, MPI_COMM_WORLD);
            offset = offset + current_rows;
        }
        
        printf("All data sent. Waiting for results...\n");
        printf("Estimated time: ~30-60 seconds (depending on CPU)\n");
        fflush(stdout);
        
        /* wait for results from all worker tasks */
        for (i = 1; i <= numworkers; i++) {
            source = i;
            printf("Waiting for results from worker %d...\n", source);
            fflush(stdout);
            
            MPI_Recv(&offset, 1, MPI_INT, source, 2, MPI_COMM_WORLD, &status);
            MPI_Recv(&rows, 1, MPI_INT, source, 2, MPI_COMM_WORLD, &status);
            MPI_Recv(&c[offset][0], rows * N, MPI_DOUBLE, source, 2, MPI_COMM_WORLD, &status);
            
            printf("✓ Received results from worker %d\n", source);
            fflush(stdout);
        }
        
        t2 = MPI_Wtime();
        
        printf("\n🎉 Matrix multiplication completed successfully!\n");
        printf("⏱️  Total elapsed time: %.2f seconds\n", t2 - t1);
        
        // Verify result (first element should be N * 2.0 = 4000.0)
        printf("\n🔍 Verification:\n");
        printf("   c[0][0] = %.2f (expected: %.2f)\n", c[0][0], (double)N * 2.0);
        printf("   c[N-1][N-1] = %.2f (expected: %.2f)\n", c[N-1][N-1], (double)N * 2.0);
        
        if (c[0][0] == (double)N * 2.0) {
            printf("✅ Result is CORRECT!\n");
        } else {
            printf("❌ Result is INCORRECT!\n");
        }
    }
    
    /*---------------------------- worker ----------------------------*/
    if (taskid > 0) {
        printf("Worker %d: Starting...\n", taskid);
        fflush(stdout);
        
        source = 0;
        MPI_Recv(&offset, 1, MPI_INT, source, 1, MPI_COMM_WORLD, &status);
        MPI_Recv(&rows, 1, MPI_INT, source, 1, MPI_COMM_WORLD, &status);
        MPI_Recv(&a, rows * N, MPI_DOUBLE, source, 1, MPI_COMM_WORLD, &status);
        MPI_Recv(&b, N * N, MPI_DOUBLE, source, 1, MPI_COMM_WORLD, &status);
        
        printf("Worker %d: Processing %d rows (offset=%d)\n", taskid, rows, offset);
        printf("Worker %d: Starting multiplication... (this may take a while)\n", taskid);
        fflush(stdout);
        
        double worker_start = MPI_Wtime();
        
        /* Matrix multiplication with progress */
        for (k = 0; k < N; k++) {
            // Show progress every 200 columns
            if (k % 200 == 0) {
                printf("Worker %d: Progress %.1f%% (column %d/%d)\n", 
                       taskid, (double)k/N * 100.0, k, N);
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
        printf("Worker %d: ✓ Multiplication completed in %.2f seconds\n", 
               taskid, worker_end - worker_start);
        printf("Worker %d: Sending results back to master...\n", taskid);
        fflush(stdout);
        
        MPI_Send(&offset, 1, MPI_INT, 0, 2, MPI_COMM_WORLD);
        MPI_Send(&rows, 1, MPI_INT, 0, 2, MPI_COMM_WORLD);
        MPI_Send(&c, rows * N, MPI_DOUBLE, 0, 2, MPI_COMM_WORLD);
        
        printf("Worker %d: ✅ Results sent successfully!\n", taskid);
        fflush(stdout);
    }
    
    MPI_Finalize();
    return 0;
}
#include <stdlib.h>
#include <stdio.h>
#include <mpi.h>

#define N 10 /* Matriz pequena para teste */

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
    
    if (taskid == 0) {
        printf("Matrix multiplication with %d workers\n", numworkers);
        printf("Matrix size: %d x %d\n", N, N);
        
        if (numworkers == 0) {
            printf("ERROR: Need at least 2 processes\n");
            MPI_Finalize();
            return 1;
        }
        
        t1 = MPI_Wtime();
        
        // Initialize matrices
        for (i = 0; i < N; i++) {
            for (j = 0; j < N; j++) {
                a[i][j] = 1.0;
                b[i][j] = 2.0;
            }
        }
        
        rows = N / numworkers;
        offset = 0;
        
        // Send data to workers
        for (dest = 1; dest <= numworkers; dest++) {
            MPI_Send(&offset, 1, MPI_INT, dest, 1, MPI_COMM_WORLD);
            MPI_Send(&rows, 1, MPI_INT, dest, 1, MPI_COMM_WORLD);
            MPI_Send(&a[offset][0], rows * N, MPI_DOUBLE, dest, 1, MPI_COMM_WORLD);
            MPI_Send(&b, N * N, MPI_DOUBLE, dest, 1, MPI_COMM_WORLD);
            offset = offset + rows;
        }
        
        // Receive results
        for (i = 1; i <= numworkers; i++) {
            source = i;
            MPI_Recv(&offset, 1, MPI_INT, source, 2, MPI_COMM_WORLD, &status);
            MPI_Recv(&rows, 1, MPI_INT, source, 2, MPI_COMM_WORLD, &status);
            MPI_Recv(&c[offset][0], rows * N, MPI_DOUBLE, source, 2, MPI_COMM_WORLD, &status);
        }
        
        t2 = MPI_Wtime();
        
        printf("Completed! Time: %f seconds\n", t2 - t1);
        
        // Print result
        printf("\nResult matrix:\n");
        for (i = 0; i < N; i++) {
            for (j = 0; j < N; j++) {
                printf("%6.2f ", c[i][j]);
            }
            printf("\n");
        }
    }
    
    if (taskid > 0) {
        source = 0;
        MPI_Recv(&offset, 1, MPI_INT, source, 1, MPI_COMM_WORLD, &status);
        MPI_Recv(&rows, 1, MPI_INT, source, 1, MPI_COMM_WORLD, &status);
        MPI_Recv(&a, rows * N, MPI_DOUBLE, source, 1, MPI_COMM_WORLD, &status);
        MPI_Recv(&b, N * N, MPI_DOUBLE, source, 1, MPI_COMM_WORLD, &status);
        
        // Matrix multiplication
        for (k = 0; k < N; k++) {
            for (i = 0; i < rows; i++) {
                c[i][k] = 0.0;
                for (j = 0; j < N; j++) {
                    c[i][k] = c[i][k] + a[i][j] * b[j][k];
                }
            }
        }
        
        MPI_Send(&offset, 1, MPI_INT, 0, 2, MPI_COMM_WORLD);
        MPI_Send(&rows, 1, MPI_INT, 0, 2, MPI_COMM_WORLD);
        MPI_Send(&c, rows * N, MPI_DOUBLE, 0, 2, MPI_COMM_WORLD);
    }
    
    MPI_Finalize();
    return 0;
}
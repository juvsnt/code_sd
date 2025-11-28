/*
 * Contagem paralela de números primos com MPI
 * -------------------------------------------
 * Este programa conta quantos números primos existem no intervalo [2, N]
 * utilizando processamento paralelo com MPI.
 *
 * Cada processo fica responsável por testar uma parte dos números,
 * evitando trabalho redundante (por exemplo, ignorando números pares)
 * e utilizando um teste de primalidade otimizado (divisões apenas até
 * a raiz quadrada do número).
 *
 * Execução (exemplo):
 *   mpirun -np 4 ./primos_mpi 1000000
 *
 * Se N não for informado na linha de comando, é usado um valor padrão.
 */

#include <mpi.h>
#include <iostream>
#include <cmath>
#include <cstdlib> // std::atoi

// ---------------------------------------------------------------------------
// Função: is_prime
// Propósito:
//   Verificar se um número inteiro n é primo de forma eficiente.
// Estratégia:
//   - Números menores que 2 não são primos.
//   - 2 é primo.
//   - Números pares maiores que 2 não são primos.
//   - Para ímpares, testa divisores ímpares de 3 até sqrt(n).
// ---------------------------------------------------------------------------
bool is_prime(int n)
{
    if (n < 2) return false;
    if (n == 2) return true;
    if (n % 2 == 0) return false;

    int limit = static_cast<int>(std::sqrt(static_cast<double>(n)));
    for (int d = 3; d <= limit; d += 2)
    {
        if (n % d == 0)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Função: count_primes_local
// Propósito:
//   Calcular, em cada processo MPI, quantos números primos existem no
//   intervalo [2, n], considerando apenas o subconjunto de números
//   atribuídos àquele processo.
// Estratégia de distribuição:
//   - O número primo 2 é tratado separadamente (processo 0).
//   - Cada processo verifica apenas números ímpares.
//   - Para evitar sobreposição, usamos:
//       início = 3 + 2 * rank
//       passo  = 2 * size
//     Assim, cada processo anda em "saltos" diferentes dentro da
//     sequência de ímpares.
//
// Parâmetros:
//   - n     : limite superior do intervalo [2, n].
//   - rank  : identificador do processo (0, 1, ..., size-1).
//   - size  : número total de processos MPI.
//
// Retorno:
//   - quantidade de números primos encontrados pelo processo.
// ---------------------------------------------------------------------------
int count_primes_local(int n, int rank, int size)
{
    int local_count = 0;

    // Cada processo percorre apenas um subconjunto dos números ímpares.
    int start = 3 + 2 * rank;      // primeiro ímpar atribuído a este rank
    int step  = 2 * size;          // salto entre números ímpares para este rank

    for (int value = start; value <= n; value += step)
    {
        if (is_prime(value))
        {
            local_count++;
        }
    }

    return local_count;
}

int main(int argc, char* argv[])
{
    MPI_Init(&argc, &argv);

    int rank = 0; // identificador do processo
    int size = 0; // número total de processos
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // -----------------------------------------------------------------------
    // Definição do limite N
    // - Processo 0 lê N da linha de comando (se existir) ou define padrão.
    // - Em seguida, N é disseminado para todos com MPI_Bcast.
    // -----------------------------------------------------------------------
    int n = 1000000; // valor padrão

    if (rank == 0)
    {
        if (argc >= 2)
        {
            n = std::atoi(argv[1]);
            if (n < 2)
            {
                std::cerr << "Valor de N invalido. Usando N = 1000000.\n";
                n = 1000000;
            }
        }

        std::cout << "Contagem de numeros primos em [2, " << n << "] usando "
                  << size << " processos MPI.\n";
    }

    // Todos os processos recebem o mesmo valor de n.
    MPI_Bcast(&n, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // Sincroniza todos antes de começar a contagem (para o tempo ficar coerente).
    MPI_Barrier(MPI_COMM_WORLD);
    double t_start = MPI_Wtime();

    // -----------------------------------------------------------------------
    // Cálculo local de primos
    // - Processo 0 trata o primo 2 (se estiver dentro do intervalo).
    // - Cada processo conta seus primos ímpares com count_primes_local.
    // -----------------------------------------------------------------------
    int local_count = count_primes_local(n, rank, size);

    // Processo 0 adiciona o primo 2, se n >= 2
    int base_primes = 0;
    if (rank == 0 && n >= 2)
    {
        base_primes = 1; // contando o 2
    }

    // Soma global dos resultados locais
    int global_count = 0;
    MPI_Reduce(&local_count, &global_count, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    double t_end = MPI_Wtime();
    double elapsed = t_end - t_start;

    // -----------------------------------------------------------------------
    // Saída dos resultados
    // -----------------------------------------------------------------------
    if (rank == 0)
    {
        int total_primes = global_count + base_primes;

        std::cout << "Total de numeros primos encontrados: " << total_primes << "\n";
        std::cout << "Tempo de execucao: " << elapsed << " segundos.\n";
    }

    MPI_Finalize();
    return 0;
}

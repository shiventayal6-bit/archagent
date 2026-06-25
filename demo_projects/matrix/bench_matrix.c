#include "matrix.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define N 1024

int main(void) {
    double *matrix = malloc((size_t) N * (size_t) N * sizeof(double));
    if (!matrix) return 1;

    for (int i = 0; i < N * N; i++) matrix[i] = (double) i;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    double sum = matrix_sum(matrix, N);

    clock_gettime(CLOCK_MONOTONIC, &end);

    long ns = (end.tv_sec - start.tv_sec) * 1000000000L +
              (end.tv_nsec - start.tv_nsec);

    printf("sum = %f\n", sum);
    printf("BENCHMARK_NS=%ld\n", ns);

    free(matrix);
    return 0;
}

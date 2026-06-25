#include "matrix.h"

double matrix_sum(double *matrix, int n) {
    double sum = 0.0;
    for (int col = 0; col < n; col++)
        for (int row = 0; row < n; row++)
            sum += matrix[row * n + col];
    return sum;
}

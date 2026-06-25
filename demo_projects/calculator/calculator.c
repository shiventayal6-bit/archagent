#include <stdio.h>
#include "calculator.h"

double calculate(double a, char op, double b) {
    switch (op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': return b != 0 ? a / b : 0;
        default:  return 0;
    }
}

int main(void) {
    printf("2 + 3 = %.0f\n", calculate(2, '+', 3));
    printf("10 / 2 = %.0f\n", calculate(10, '/', 2));
    return 0;
}

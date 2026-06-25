#include "wordcount.h"
#include <stdio.h>

int main(void) {
    int failures = 0;

    if (count_words("hello world") != 2) {
        printf("FAIL: 'hello world' expected 2, got %d\n", count_words("hello world"));
        failures++;
    }
    if (count_words("hello  world") != 2) {
        printf("FAIL: 'hello  world' expected 2, got %d\n", count_words("hello  world"));
        failures++;
    }
    if (count_words("\nhello\nworld\n") != 2) {
        printf("FAIL: '\\nhello\\nworld\\n' expected 2, got %d\n", count_words("\nhello\nworld\n"));
        failures++;
    }
    if (count_words("") != 0) {
        printf("FAIL: '' expected 0, got %d\n", count_words(""));
        failures++;
    }

    if (failures == 0) {
        printf("All wordcount tests passed\n");
        return 0;
    }
    printf("%d test(s) failed\n", failures);
    return 1;
}

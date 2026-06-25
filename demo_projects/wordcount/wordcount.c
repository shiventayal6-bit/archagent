#include "wordcount.h"

int count_words(const char *s) {
    int count = 0;
    int i = 0;
    while (s[i] != '\0') {
        if (s[i] == ' ') count++;
        i++;
    }
    return count;
}

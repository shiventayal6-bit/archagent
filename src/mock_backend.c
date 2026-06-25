// Created by st3125 on 2026/6/11
// ArchAgent IDE - Mock backend

#include "mock_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void model_response_free(ModelResponse *response) {
    if (response && response->text) {
        free(response->text);
        response->text = NULL;
    }
}

// calculator patch - adds exponentiation support
// hunk line numbers match demo_projects/calculator/calculator.c exactly
static const char *CALC_RESPONSE =
    "PLAN:\n"
    "- Add math.h include for pow() function\n"
    "- Add ^ case to the switch statement in calculate()\n"
    "\n"
    "PATCH:\n"
    "```diff\n"
    "--- a/calculator.c\n"
    "+++ b/calculator.c\n"
    "@@ -1,11 +1,12 @@\n"
    " #include <stdio.h>\n"
    "+#include <math.h>\n"
    " #include \"calculator.h\"\n"
    " \n"
    " double calculate(double a, char op, double b) {\n"
    "     switch (op) {\n"
    "         case '+': return a + b;\n"
    "         case '-': return a - b;\n"
    "         case '*': return a * b;\n"
    "         case '/': return b != 0 ? a / b : 0;\n"
    "+        case '^': return pow(a, b);\n"
    "         default:  return 0;\n"
    "     }\n"
    " }\n"
    "```\n"
    "\n"
    "TESTS:\n"
    "- make\n"
    "- make test\n";

// wordcount patch - fixes multiple spaces bug
// hunk line numbers match demo_projects/wordcount/wordcount.c exactly
static const char *WORDCOUNT_RESPONSE =
    "PLAN:\n"
    "- Fix word counting to skip consecutive whitespace\n"
    "- Use a flag to track whether we are inside a word\n"
    "\n"
    "PATCH:\n"
    "```diff\n"
    "--- a/wordcount.c\n"
    "+++ b/wordcount.c\n"
    "@@ -1,12 +1,14 @@\n"
    " #include \"wordcount.h\"\n"
    " \n"
    " int count_words(const char *s) {\n"
    "     int count = 0;\n"
    "-    int i = 0;\n"
    "-    while (s[i] != '\\0') {\n"
    "-        if (s[i] == ' ') count++;\n"
    "-        i++;\n"
    "+    int in_word = 0;\n"
    "+    while (*s) {\n"
    "+        if (*s == ' ' || *s == '\\t' || *s == '\\n') {\n"
    "+            in_word = 0;\n"
    "+        } else if (!in_word) {\n"
    "+            in_word = 1;\n"
    "+            count++;\n"
    "+        }\n"
    "+        s++;\n"
    "     }\n"
    "     return count;\n"
    " }\n"
    "```\n"
    "\n"
    "TESTS:\n"
    "- make\n"
    "- make test\n";

// matrix patch - fixes loop order for cache locality
// hunk line numbers match demo_projects/matrix/matrix.c exactly
static const char *MATRIX_RESPONSE =
    "PLAN:\n"
    "- Swap loop order from col-first to row-first\n"
    "- Row-first access matches C row-major memory layout\n"
    "\n"
    "PATCH:\n"
    "```diff\n"
    "--- a/matrix.c\n"
    "+++ b/matrix.c\n"
    "@@ -1,10 +1,10 @@\n"
    " #include \"matrix.h\"\n"
    " \n"
    " double matrix_sum(double *matrix, int n) {\n"
    "     double sum = 0.0;\n"
    "-    for (int col = 0; col < n; col++)\n"
    "-        for (int row = 0; row < n; row++)\n"
    "+    for (int row = 0; row < n; row++)\n"
    "+        for (int col = 0; col < n; col++)\n"
    "             sum += matrix[row * n + col];\n"
    "     return sum;\n"
    " }\n"
    "```\n"
    "\n"
    "TESTS:\n"
    "- make\n"
    "- make bench\n";

// unsafe patch - targets an absolute path to demonstrate rejection
static const char *UNSAFE_RESPONSE =
    "PLAN:\n"
    "- This patch is intentionally unsafe for demonstration\n"
    "\n"
    "PATCH:\n"
    "```diff\n"
    "--- a//etc/passwd\n"
    "+++ b//etc/passwd\n"
    "@@ -1,1 +1,2 @@\n"
    " root:x:0:0:root:/root:/bin/bash\n"
    "+hacker:x:0:0:hacker:/root:/bin/bash\n"
    "```\n"
    "\n"
    "TESTS:\n"
    "- make\n";

bool mock_backend_generate(const char *request, ModelResponse *out) {
    if (!request || !out) return false;
    out->exit_code  = 0;
    out->timed_out  = false;
    out->text       = NULL;

    const char *response = NULL;

    if (strstr(request, "exponentiation") || strstr(request, "^")) {
        response = CALC_RESPONSE;
    } else if (strstr(request, "wordcount") || strstr(request, "word count") ||
               strstr(request, "spaces")    || strstr(request, "newlines")) {
        response = WORDCOUNT_RESPONSE;
    } else if (strstr(request, "matrix") || strstr(request, "cache")) {
        response = MATRIX_RESPONSE;
    } else if (strstr(request, "unsafe")) {
        response = UNSAFE_RESPONSE;
    } else {
        response = CALC_RESPONSE;
    }

    out->text = strdup(response);
    if (!out->text) return false;
    return true;
}

// ArchAgent IDE - C-to-A64 Playground: codegen unit tests

#include "c2asm_lexer.h"
#include "c2asm_parser.h"
#include "c2asm_codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg)                                       \
    do {                                                       \
        if (!(cond)) {                                         \
            printf("FAIL: %s\n", msg);                         \
            failures++;                                        \
        }                                                      \
    } while (0)

// compile a snippet to assembly text; caller frees. returns NULL on error.
static char *gen(const char *src) {
    Lexer lex;
    lexer_init(&lex, src);
    Parser p;
    if (!parser_init(&p, &lex, NULL, 0)) return NULL;
    AstNode *prog = parser_parse(&p);
    if (!prog) return NULL;

    Codegen cg;
    if (!codegen_init(&cg)) { ast_free(prog); return NULL; }
    char *asm_text = NULL;
    if (codegen_generate(&cg, prog)) {
        asm_text = codegen_get_assembly(&cg);
    }
    codegen_free(&cg);
    ast_free(prog);
    return asm_text;
}

static void test_simple_return(void) {
    char *a = gen("int a = 5; return a;");
    CHECK(a != NULL, "simple return should generate");
    if (!a) return;
    CHECK(strstr(a, "movz x16, #5") != NULL, "loads 5 into a temp");
    CHECK(strstr(a, "mov x1, x16") != NULL, "moves temp into var x1");
    CHECK(strstr(a, "mov x0, x1") != NULL, "returns var via x0");
    CHECK(strstr(a, "and x0, x0, x0") != NULL, "emits halt");
    free(a);
}

static void test_addition(void) {
    char *a = gen("int a = 2; int b = 3; return a + b;");
    CHECK(a != NULL, "addition should generate");
    if (!a) return;
    CHECK(strstr(a, "add ") != NULL, "emits add instruction");
    CHECK(strstr(a, "and x0, x0, x0") != NULL, "emits halt");
    free(a);
}

static void test_multiplication(void) {
    char *a = gen("int a = 6; int b = 7; return a * b;");
    CHECK(a != NULL, "multiplication should generate");
    if (!a) return;
    CHECK(strstr(a, "mul ") != NULL, "emits mul instruction");
    free(a);
}

static void test_while_loop(void) {
    char *a = gen("int n = 5; int r = 1; while (n > 1) { r = r * n; n = n - 1; } return r;");
    CHECK(a != NULL, "while loop should generate");
    if (!a) return;
    CHECK(strstr(a, "lwhile0start:") != NULL, "emits while start label");
    CHECK(strstr(a, "lwhile0end:") != NULL, "emits while end label");
    CHECK(strstr(a, "cmp ") != NULL, "emits cmp");
    CHECK(strstr(a, "b.le ") != NULL, "emits inverse branch for >");
    CHECK(strstr(a, "b lwhile0start") != NULL, "emits back-branch to start");
    free(a);
}

static void test_if_else(void) {
    char *a = gen("int a = 1; int b = 2; if (a < b) { return b; } else { return a; }");
    CHECK(a != NULL, "if/else should generate");
    if (!a) return;
    CHECK(strstr(a, "b.ge ") != NULL, "emits inverse branch for <");
    CHECK(strstr(a, "lif0else:") != NULL, "emits else label");
    CHECK(strstr(a, "lif0end:") != NULL, "emits end label");
    free(a);
}

static void test_division_rejected(void) {
    char *a = gen("int a = 6 / 2; return a;");
    CHECK(a == NULL, "division should be rejected (no assembly produced)");
    free(a);
}

static void test_large_constant(void) {
    char *a = gen("int a = 100000; return a;");
    CHECK(a != NULL, "large constant should generate");
    if (!a) return;
    CHECK(strstr(a, "movz ") != NULL, "uses movz");
    CHECK(strstr(a, "movk ") != NULL, "uses movk for high bits");
    free(a);
}

int main(void) {
    test_simple_return();
    test_addition();
    test_multiplication();
    test_while_loop();
    test_if_else();
    test_division_rejected();
    test_large_constant();

    if (failures == 0) {
        printf("test_c2asm_codegen: all tests passed\n");
        return 0;
    }
    printf("test_c2asm_codegen: %d failure(s)\n", failures);
    return 1;
}

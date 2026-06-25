// ArchAgent IDE - C-to-A64 Playground: lexer unit tests

#include "c2asm_lexer.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond, msg)                                       \
    do {                                                       \
        if (!(cond)) {                                         \
            printf("FAIL: %s\n", msg);                         \
            failures++;                                        \
        }                                                      \
    } while (0)

static void test_basic_declaration(void) {
    Lexer lex;
    lexer_init(&lex, "int x = 5;");

    Token t = lexer_next(&lex);
    CHECK(t.type == TOK_INT, "expected TOK_INT");

    t = lexer_next(&lex);
    CHECK(t.type == TOK_IDENT && strcmp(t.text, "x") == 0, "expected ident x");

    t = lexer_next(&lex);
    CHECK(t.type == TOK_EQ, "expected TOK_EQ");

    t = lexer_next(&lex);
    CHECK(t.type == TOK_NUMBER && t.int_val == 5, "expected number 5");

    t = lexer_next(&lex);
    CHECK(t.type == TOK_SEMI, "expected TOK_SEMI");

    t = lexer_next(&lex);
    CHECK(t.type == TOK_EOF, "expected TOK_EOF");
}

static void test_comments_skipped(void) {
    Lexer lex;
    lexer_init(&lex,
        "// line comment\n"
        "int /* block */ y = 0x10; // trailing\n");

    Token t = lexer_next(&lex);
    CHECK(t.type == TOK_INT, "comment: expected TOK_INT");

    t = lexer_next(&lex);
    CHECK(t.type == TOK_IDENT && strcmp(t.text, "y") == 0, "comment: expected ident y");

    t = lexer_next(&lex);
    CHECK(t.type == TOK_EQ, "comment: expected TOK_EQ");

    t = lexer_next(&lex);
    CHECK(t.type == TOK_NUMBER && t.int_val == 16, "comment: expected hex 0x10 == 16");

    t = lexer_next(&lex);
    CHECK(t.type == TOK_SEMI, "comment: expected TOK_SEMI");

    t = lexer_next(&lex);
    CHECK(t.type == TOK_EOF, "comment: expected EOF");
}

static void test_two_char_operators(void) {
    Lexer lex;
    lexer_init(&lex, "== != <= >= < > =");

    TokenType expected[] = {
        TOK_EQEQ, TOK_NEQ, TOK_LE, TOK_GE, TOK_LT, TOK_GT, TOK_EQ, TOK_EOF
    };
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        Token t = lexer_next(&lex);
        CHECK(t.type == expected[i], "operator sequence mismatch");
    }
}

static void test_keywords(void) {
    Lexer lex;
    lexer_init(&lex, "return if else while int");
    TokenType expected[] = {
        TOK_RETURN, TOK_IF, TOK_ELSE, TOK_WHILE, TOK_INT, TOK_EOF
    };
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++) {
        Token t = lexer_next(&lex);
        CHECK(t.type == expected[i], "keyword mismatch");
    }
}

static void test_unknown_char_error(void) {
    Lexer lex;
    lexer_init(&lex, "int x @ 3;");
    (void) lexer_next(&lex); // int
    (void) lexer_next(&lex); // x
    Token t = lexer_next(&lex); // @
    CHECK(t.type == TOK_ERROR, "expected TOK_ERROR for '@'");
}

static void test_peek_does_not_consume(void) {
    Lexer lex;
    lexer_init(&lex, "int x;");
    Token p = lexer_peek(&lex);
    CHECK(p.type == TOK_INT, "peek: expected TOK_INT");
    Token n = lexer_next(&lex);
    CHECK(n.type == TOK_INT, "peek: next should still be TOK_INT");
    Token n2 = lexer_next(&lex);
    CHECK(n2.type == TOK_IDENT, "peek: second next should be ident");
}

int main(void) {
    test_basic_declaration();
    test_comments_skipped();
    test_two_char_operators();
    test_keywords();
    test_unknown_char_error();
    test_peek_does_not_consume();

    if (failures == 0) {
        printf("test_c2asm_lexer: all tests passed\n");
        return 0;
    }
    printf("test_c2asm_lexer: %d failure(s)\n", failures);
    return 1;
}

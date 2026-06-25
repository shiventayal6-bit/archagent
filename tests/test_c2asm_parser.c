// ArchAgent IDE - C-to-A64 Playground: parser unit tests

#include "c2asm_lexer.h"
#include "c2asm_parser.h"

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

static AstNode *parse_source(const char *src, Lexer *lex, Parser *p) {
    lexer_init(lex, src);
    if (!parser_init(p, lex, NULL, 0)) return NULL;
    return parser_parse(p);
}

static void test_decl_and_return(void) {
    Lexer lex; Parser p;
    AstNode *prog = parse_source("int a = 5; return a;", &lex, &p);
    CHECK(prog != NULL, "program should parse");
    if (!prog) return;
    CHECK(prog->type == AST_PROGRAM, "root is AST_PROGRAM");
    CHECK(prog->stmt_count == 2, "two statements expected");
    CHECK(prog->stmts[0]->type == AST_DECL, "first stmt is decl");
    CHECK(strcmp(prog->stmts[0]->name, "a") == 0, "decl name is a");
    CHECK(prog->stmts[0]->left != NULL && prog->stmts[0]->left->type == AST_NUMBER,
          "decl initialised with number");
    CHECK(prog->stmts[1]->type == AST_RETURN, "second stmt is return");
    ast_free(prog);
}

static void test_precedence(void) {
    Lexer lex; Parser p;
    // a + b * 2  =>  add(a, mul(b, 2))
    AstNode *prog = parse_source("int a = 1; int b = 2; int c = a + b * 2;", &lex, &p);
    CHECK(prog != NULL, "precedence program should parse");
    if (!prog) return;
    AstNode *decl_c = prog->stmts[2];
    CHECK(decl_c->type == AST_DECL, "third stmt is decl");
    AstNode *expr = decl_c->left;
    CHECK(expr->type == AST_BINOP && strcmp(expr->op, "+") == 0, "top op is +");
    CHECK(expr->right->type == AST_BINOP && strcmp(expr->right->op, "*") == 0,
          "right child is *");
    ast_free(prog);
}

static void test_if_else(void) {
    Lexer lex; Parser p;
    AstNode *prog = parse_source(
        "int a = 1; if (a < 2) { a = 3; } else { a = 4; }", &lex, &p);
    CHECK(prog != NULL, "if/else should parse");
    if (!prog) return;
    AstNode *if_node = prog->stmts[1];
    CHECK(if_node->type == AST_IF, "second stmt is if");
    CHECK(if_node->cond != NULL && if_node->cond->type == AST_COND, "if has condition");
    CHECK(strcmp(if_node->cond->op, "<") == 0, "condition op is <");
    CHECK(if_node->body != NULL && if_node->body->type == AST_BLOCK, "if body is block");
    CHECK(if_node->else_ != NULL && if_node->else_->type == AST_BLOCK, "else body is block");
    ast_free(prog);
}

static void test_while(void) {
    Lexer lex; Parser p;
    AstNode *prog = parse_source(
        "int n = 5; while (n > 0) { n = n - 1; }", &lex, &p);
    CHECK(prog != NULL, "while should parse");
    if (!prog) return;
    AstNode *w = prog->stmts[1];
    CHECK(w->type == AST_WHILE, "second stmt is while");
    CHECK(w->cond->type == AST_COND && strcmp(w->cond->op, ">") == 0, "while cond > 0");
    CHECK(w->body->type == AST_BLOCK && w->body->stmt_count == 1, "while body one stmt");
    ast_free(prog);
}

static void test_parse_error(void) {
    Lexer lex; Parser p;
    AstNode *prog = parse_source("int = 5;", &lex, &p);
    CHECK(prog == NULL, "missing identifier should fail to parse");
    CHECK(p.error[0] != '\0', "error message should be set");
    if (prog) ast_free(prog);
}

int main(void) {
    test_decl_and_return();
    test_precedence();
    test_if_else();
    test_while();
    test_parse_error();

    if (failures == 0) {
        printf("test_c2asm_parser: all tests passed\n");
        return 0;
    }
    printf("test_c2asm_parser: %d failure(s)\n", failures);
    return 1;
}

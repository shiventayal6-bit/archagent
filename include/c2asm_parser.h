// ArchAgent — C-Mini parser
// Recursive-descent parser that produces an AST from the C-Mini token stream.
// Supports declarations, assignments, arithmetic, comparisons, if/else,
// while loops, blocks, and return statements.

#ifndef ARCHAGENT_C2ASM_PARSER_H
#define ARCHAGENT_C2ASM_PARSER_H

#include "c2asm_lexer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// AST node types
typedef enum {
    AST_PROGRAM,
    AST_DECL,       // int x = expr;
    AST_ASSIGN,     // x = expr;
    AST_RETURN,     // return expr;
    AST_IF,         // if (cond) block [else block]
    AST_WHILE,      // while (cond) block
    AST_BLOCK,      // { stmts... }
    AST_BINOP,      // expr op expr
    AST_UNARY,      // -expr
    AST_IDENT,      // variable name
    AST_NUMBER,     // integer literal
    AST_COND,       // condition (expr op expr)
} AstNodeType;

// Forward declaration
typedef struct AstNode AstNode;

struct AstNode {
    AstNodeType type;
    int         line;

    // For AST_BINOP, AST_COND: operator character/string
    char        op[4];  // "+", "-", "*", "/", "==", "!=", "<", "<=", ">", ">="

    // For AST_IDENT, AST_DECL, AST_ASSIGN: name
    char        name[256];

    // For AST_NUMBER: value
    int64_t     int_val;

    // Children: left/right for binary, cond/body/else for if/while
    AstNode    *left;
    AstNode    *right;
    AstNode    *cond;    // for if/while: the condition node
    AstNode    *body;    // for if/while: the body block
    AstNode    *else_;   // for if: the else block

    // For AST_PROGRAM, AST_BLOCK: list of statements
    AstNode   **stmts;
    size_t      stmt_count;
    size_t      stmt_cap;
};

typedef struct {
    Lexer      *lex;
    Token       current;
    Token       peek;
    char        error[512];
    int         error_line;
    int         error_col;
} Parser;

AstNode *ast_new(AstNodeType type, int line);
void     ast_free(AstNode *node);

bool     parser_init(Parser *p, Lexer *lex, char *err_out, size_t err_size);
AstNode *parser_parse(Parser *p);  // returns AST_PROGRAM or NULL on error

#endif // ARCHAGENT_C2ASM_PARSER_H

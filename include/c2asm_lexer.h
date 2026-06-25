// ArchAgent IDE - C-to-A64 Playground: lexer
// Tokenises a small C-Mini subset into a token stream.

#ifndef ARCHAGENT_C2ASM_LEXER_H
#define ARCHAGENT_C2ASM_LEXER_H

#include <stddef.h>
#include <stdint.h>

// Token types
typedef enum {
    TOK_INT,        // "int" keyword
    TOK_RETURN,     // "return"
    TOK_IF,         // "if"
    TOK_ELSE,       // "else"
    TOK_WHILE,      // "while"
    TOK_IDENT,      // identifier
    TOK_NUMBER,     // integer literal
    TOK_PLUS,       // +
    TOK_MINUS,      // -
    TOK_STAR,       // *
    TOK_SLASH,      // /
    TOK_EQ,         // =
    TOK_EQEQ,       // ==
    TOK_NEQ,        // !=
    TOK_LT,         // <
    TOK_LE,         // <=
    TOK_GT,         // >
    TOK_GE,         // >=
    TOK_LPAREN,     // (
    TOK_RPAREN,     // )
    TOK_LBRACE,     // {
    TOK_RBRACE,     // }
    TOK_SEMI,       // ;
    TOK_EOF,        // end of input
    TOK_ERROR,      // lexer error
} TokenType;

typedef struct {
    TokenType type;
    char      text[256];   // token text (identifier or literal)
    int64_t   int_val;     // for TOK_NUMBER
    int       line;
    int       col;
} Token;

typedef struct {
    const char *src;
    size_t      pos;
    size_t      len;
    int         line;
    int         col;
    char        error[256];
} Lexer;

void  lexer_init(Lexer *lex, const char *source);
Token lexer_next(Lexer *lex);
Token lexer_peek(Lexer *lex);

#endif // ARCHAGENT_C2ASM_LEXER_H

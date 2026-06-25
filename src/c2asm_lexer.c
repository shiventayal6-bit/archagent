// ArchAgent IDE - C-to-A64 Playground: lexer implementation

#include "c2asm_lexer.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void lexer_init(Lexer *lex, const char *source) {
    lex->src   = source ? source : "";
    lex->pos   = 0;
    lex->len   = strlen(lex->src);
    lex->line  = 1;
    lex->col   = 1;
    lex->error[0] = '\0';
}

// advance one character, tracking line/column
static char advance(Lexer *lex) {
    char c = lex->src[lex->pos];
    lex->pos++;
    if (c == '\n') {
        lex->line++;
        lex->col = 1;
    } else {
        lex->col++;
    }
    return c;
}

static char peek_char(const Lexer *lex) {
    if (lex->pos >= lex->len) return '\0';
    return lex->src[lex->pos];
}

static char peek_char2(const Lexer *lex) {
    if (lex->pos + 1 >= lex->len) return '\0';
    return lex->src[lex->pos + 1];
}

// skip whitespace and both styles of comment
static void skip_trivia(Lexer *lex) {
    for (;;) {
        char c = peek_char(lex);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance(lex);
            continue;
        }
        if (c == '/' && peek_char2(lex) == '/') {
            // line comment
            while (peek_char(lex) != '\0' && peek_char(lex) != '\n') {
                advance(lex);
            }
            continue;
        }
        if (c == '/' && peek_char2(lex) == '*') {
            // block comment
            advance(lex); // '/'
            advance(lex); // '*'
            while (peek_char(lex) != '\0') {
                if (peek_char(lex) == '*' && peek_char2(lex) == '/') {
                    advance(lex);
                    advance(lex);
                    break;
                }
                advance(lex);
            }
            continue;
        }
        break;
    }
}

static Token make_token(TokenType type, int line, int col) {
    Token t;
    t.type    = type;
    t.text[0] = '\0';
    t.int_val = 0;
    t.line    = line;
    t.col     = col;
    return t;
}

static Token make_error(Lexer *lex, int line, int col, const char *msg) {
    Token t = make_token(TOK_ERROR, line, col);
    snprintf(t.text, sizeof(t.text), "%s", msg);
    snprintf(lex->error, sizeof(lex->error),
             "line %d col %d: %s", line, col, msg);
    return t;
}

static bool ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_';
}

static bool ident_part(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

static TokenType keyword_type(const char *s) {
    if (strcmp(s, "int")    == 0) return TOK_INT;
    if (strcmp(s, "return") == 0) return TOK_RETURN;
    if (strcmp(s, "if")     == 0) return TOK_IF;
    if (strcmp(s, "else")   == 0) return TOK_ELSE;
    if (strcmp(s, "while")  == 0) return TOK_WHILE;
    return TOK_IDENT;
}

Token lexer_next(Lexer *lex) {
    skip_trivia(lex);

    int line = lex->line;
    int col  = lex->col;

    if (lex->pos >= lex->len) {
        return make_token(TOK_EOF, line, col);
    }

    char c = peek_char(lex);

    // identifiers and keywords
    if (ident_start(c)) {
        size_t n = 0;
        Token t = make_token(TOK_IDENT, line, col);
        while (ident_part(peek_char(lex))) {
            char ch = advance(lex);
            if (n < sizeof(t.text) - 1) {
                t.text[n++] = ch;
            }
        }
        t.text[n] = '\0';
        t.type = keyword_type(t.text);
        return t;
    }

    // numbers: decimal or hex (0x...)
    if (isdigit((unsigned char)c)) {
        Token t = make_token(TOK_NUMBER, line, col);
        size_t n = 0;
        int base = 10;

        if (c == '0' && (peek_char2(lex) == 'x' || peek_char2(lex) == 'X')) {
            // consume "0x"
            if (n < sizeof(t.text) - 1) t.text[n++] = advance(lex); // 0
            if (n < sizeof(t.text) - 1) t.text[n++] = advance(lex); // x
            base = 16;
            if (!isxdigit((unsigned char)peek_char(lex))) {
                return make_error(lex, line, col, "malformed hex literal");
            }
            while (isxdigit((unsigned char)peek_char(lex))) {
                char ch = advance(lex);
                if (n < sizeof(t.text) - 1) t.text[n++] = ch;
            }
        } else {
            while (isdigit((unsigned char)peek_char(lex))) {
                char ch = advance(lex);
                if (n < sizeof(t.text) - 1) t.text[n++] = ch;
            }
        }
        t.text[n] = '\0';

        // reject identifier characters directly following a number
        if (ident_part(peek_char(lex))) {
            return make_error(lex, line, col, "malformed number literal");
        }

        t.int_val = (int64_t) strtoll(t.text, NULL, base);
        return t;
    }

    // operators and punctuation
    advance(lex); // consume c
    switch (c) {
        case '+': return make_token(TOK_PLUS,   line, col);
        case '-': return make_token(TOK_MINUS,  line, col);
        case '*': return make_token(TOK_STAR,   line, col);
        case '/': return make_token(TOK_SLASH,  line, col);
        case '(': return make_token(TOK_LPAREN, line, col);
        case ')': return make_token(TOK_RPAREN, line, col);
        case '{': return make_token(TOK_LBRACE, line, col);
        case '}': return make_token(TOK_RBRACE, line, col);
        case ';': return make_token(TOK_SEMI,   line, col);
        case '=':
            if (peek_char(lex) == '=') { advance(lex); return make_token(TOK_EQEQ, line, col); }
            return make_token(TOK_EQ, line, col);
        case '!':
            if (peek_char(lex) == '=') { advance(lex); return make_token(TOK_NEQ, line, col); }
            return make_error(lex, line, col, "unexpected '!' (did you mean '!=' ?)");
        case '<':
            if (peek_char(lex) == '=') { advance(lex); return make_token(TOK_LE, line, col); }
            return make_token(TOK_LT, line, col);
        case '>':
            if (peek_char(lex) == '=') { advance(lex); return make_token(TOK_GE, line, col); }
            return make_token(TOK_GT, line, col);
        default: {
            char msg[64];
            snprintf(msg, sizeof(msg), "unexpected character '%c'", c);
            return make_error(lex, line, col, msg);
        }
    }
}

Token lexer_peek(Lexer *lex) {
    // save full lexer state, peek one token, restore
    size_t saved_pos  = lex->pos;
    int    saved_line = lex->line;
    int    saved_col  = lex->col;

    Token t = lexer_next(lex);

    lex->pos  = saved_pos;
    lex->line = saved_line;
    lex->col  = saved_col;
    return t;
}

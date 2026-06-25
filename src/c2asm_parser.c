// ArchAgent IDE - C-to-A64 Playground: recursive descent parser implementation

#include "c2asm_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

AstNode *ast_new(AstNodeType type, int line) {
    AstNode *n = calloc(1, sizeof(AstNode));
    if (!n) return NULL;
    n->type = type;
    n->line = line;
    return n;
}

void ast_free(AstNode *node) {
    if (!node) return;
    ast_free(node->left);
    ast_free(node->right);
    ast_free(node->cond);
    ast_free(node->body);
    ast_free(node->else_);
    for (size_t i = 0; i < node->stmt_count; i++) {
        ast_free(node->stmts[i]);
    }
    free(node->stmts);
    free(node);
}

// append a statement to a PROGRAM/BLOCK node, growing the array as needed
static bool node_add_stmt(AstNode *parent, AstNode *stmt) {
    if (parent->stmt_count >= parent->stmt_cap) {
        size_t new_cap = parent->stmt_cap == 0 ? 8 : parent->stmt_cap * 2;
        AstNode **grown = realloc(parent->stmts, new_cap * sizeof(AstNode *));
        if (!grown) return false;
        parent->stmts = grown;
        parent->stmt_cap = new_cap;
    }
    parent->stmts[parent->stmt_count++] = stmt;
    return true;
}

// ---- parser helpers ----

static void set_error(Parser *p, const Token *tok, const char *msg) {
    if (p->error[0] != '\0') return; // keep first error
    p->error_line = tok->line;
    p->error_col  = tok->col;
    snprintf(p->error, sizeof(p->error),
             "line %d col %d: %s", tok->line, tok->col, msg);
}

bool parser_init(Parser *p, Lexer *lex, char *err_out, size_t err_size) {
    p->lex = lex;
    p->error[0] = '\0';
    p->error_line = 0;
    p->error_col  = 0;
    p->current = lexer_next(lex);
    p->peek    = lexer_next(lex);

    if (p->current.type == TOK_ERROR) {
        set_error(p, &p->current, p->current.text);
        if (err_out && err_size) snprintf(err_out, err_size, "%s", p->error);
        return false;
    }
    return true;
}

static Token current(Parser *p)  { return p->current; }

// move to the next token; surfaces lexer errors
static void bump(Parser *p) {
    p->current = p->peek;
    p->peek = lexer_next(p->lex);
    if (p->peek.type == TOK_ERROR) {
        set_error(p, &p->peek, p->peek.text);
    }
}

static bool check(Parser *p, TokenType type) {
    return p->current.type == type;
}

// consume a token of an expected type, recording an error otherwise
static bool expect(Parser *p, TokenType type, const char *what) {
    if (p->current.type == type) {
        bump(p);
        return true;
    }
    char msg[128];
    snprintf(msg, sizeof(msg), "expected %s", what);
    set_error(p, &p->current, msg);
    return false;
}

// forward declarations for the grammar
static AstNode *parse_statement(Parser *p);
static AstNode *parse_block(Parser *p);
static AstNode *parse_expression(Parser *p);
static AstNode *parse_condition(Parser *p);

// primary := NUMBER | IDENT | "(" expression ")"
static AstNode *parse_primary(Parser *p) {
    Token t = current(p);

    if (t.type == TOK_NUMBER) {
        AstNode *n = ast_new(AST_NUMBER, t.line);
        if (!n) return NULL;
        n->int_val = t.int_val;
        bump(p);
        return n;
    }
    if (t.type == TOK_IDENT) {
        AstNode *n = ast_new(AST_IDENT, t.line);
        if (!n) return NULL;
        snprintf(n->name, sizeof(n->name), "%s", t.text);
        bump(p);
        return n;
    }
    if (t.type == TOK_LPAREN) {
        bump(p);
        AstNode *inner = parse_expression(p);
        if (!inner) return NULL;
        if (!expect(p, TOK_RPAREN, "')'")) { ast_free(inner); return NULL; }
        return inner;
    }

    set_error(p, &t, "expected number, identifier, or '('");
    return NULL;
}

// unary := "-" unary | primary
static AstNode *parse_unary(Parser *p) {
    if (check(p, TOK_MINUS)) {
        Token t = current(p);
        bump(p);
        AstNode *operand = parse_unary(p);
        if (!operand) return NULL;
        AstNode *n = ast_new(AST_UNARY, t.line);
        if (!n) { ast_free(operand); return NULL; }
        strcpy(n->op, "-");
        n->left = operand;
        return n;
    }
    return parse_primary(p);
}

// term := unary (("*" | "/") unary)*
static AstNode *parse_term(Parser *p) {
    AstNode *left = parse_unary(p);
    if (!left) return NULL;

    while (check(p, TOK_STAR) || check(p, TOK_SLASH)) {
        Token op_tok = current(p);
        const char *op = check(p, TOK_STAR) ? "*" : "/";
        bump(p);
        AstNode *right = parse_unary(p);
        if (!right) { ast_free(left); return NULL; }
        AstNode *bin = ast_new(AST_BINOP, op_tok.line);
        if (!bin) { ast_free(left); ast_free(right); return NULL; }
        strcpy(bin->op, op);
        bin->left  = left;
        bin->right = right;
        left = bin;
    }
    return left;
}

// expression := term (("+" | "-") term)*
static AstNode *parse_expression(Parser *p) {
    AstNode *left = parse_term(p);
    if (!left) return NULL;

    while (check(p, TOK_PLUS) || check(p, TOK_MINUS)) {
        Token op_tok = current(p);
        const char *op = check(p, TOK_PLUS) ? "+" : "-";
        bump(p);
        AstNode *right = parse_term(p);
        if (!right) { ast_free(left); return NULL; }
        AstNode *bin = ast_new(AST_BINOP, op_tok.line);
        if (!bin) { ast_free(left); ast_free(right); return NULL; }
        strcpy(bin->op, op);
        bin->left  = left;
        bin->right = right;
        left = bin;
    }
    return left;
}

// condition := expression comp_op expression
static AstNode *parse_condition(Parser *p) {
    AstNode *left = parse_expression(p);
    if (!left) return NULL;

    const char *op = NULL;
    switch (current(p).type) {
        case TOK_EQEQ: op = "=="; break;
        case TOK_NEQ:  op = "!="; break;
        case TOK_LT:   op = "<";  break;
        case TOK_LE:   op = "<="; break;
        case TOK_GT:   op = ">";  break;
        case TOK_GE:   op = ">="; break;
        default:
            set_error(p, &p->current,
                      "expected comparison operator (==, !=, <, <=, >, >=)");
            ast_free(left);
            return NULL;
    }

    Token op_tok = current(p);
    bump(p);
    AstNode *right = parse_expression(p);
    if (!right) { ast_free(left); return NULL; }

    AstNode *cond = ast_new(AST_COND, op_tok.line);
    if (!cond) { ast_free(left); ast_free(right); return NULL; }
    strcpy(cond->op, op);
    cond->left  = left;
    cond->right = right;
    return cond;
}

// declaration := "int" IDENT ("=" expression)? ";"
static AstNode *parse_declaration(Parser *p) {
    Token int_tok = current(p);
    bump(p); // consume "int"

    if (!check(p, TOK_IDENT)) {
        set_error(p, &p->current, "expected identifier after 'int'");
        return NULL;
    }
    AstNode *decl = ast_new(AST_DECL, int_tok.line);
    if (!decl) return NULL;
    snprintf(decl->name, sizeof(decl->name), "%s", current(p).text);
    bump(p); // consume IDENT

    if (check(p, TOK_EQ)) {
        bump(p);
        AstNode *init = parse_expression(p);
        if (!init) { ast_free(decl); return NULL; }
        decl->left = init;
    }

    if (!expect(p, TOK_SEMI, "';'")) { ast_free(decl); return NULL; }
    return decl;
}

// assignment := IDENT "=" expression ";"
static AstNode *parse_assignment(Parser *p) {
    Token id_tok = current(p);
    AstNode *assign = ast_new(AST_ASSIGN, id_tok.line);
    if (!assign) return NULL;
    snprintf(assign->name, sizeof(assign->name), "%s", id_tok.text);
    bump(p); // consume IDENT

    if (!expect(p, TOK_EQ, "'=' in assignment")) { ast_free(assign); return NULL; }

    AstNode *value = parse_expression(p);
    if (!value) { ast_free(assign); return NULL; }
    assign->left = value;

    if (!expect(p, TOK_SEMI, "';'")) { ast_free(assign); return NULL; }
    return assign;
}

// return_stmt := "return" expression ";"
static AstNode *parse_return(Parser *p) {
    Token ret_tok = current(p);
    bump(p); // consume "return"

    AstNode *ret = ast_new(AST_RETURN, ret_tok.line);
    if (!ret) return NULL;

    AstNode *value = parse_expression(p);
    if (!value) { ast_free(ret); return NULL; }
    ret->left = value;

    if (!expect(p, TOK_SEMI, "';'")) { ast_free(ret); return NULL; }
    return ret;
}

// if_stmt := "if" "(" condition ")" block ("else" block)?
static AstNode *parse_if(Parser *p) {
    Token if_tok = current(p);
    bump(p); // consume "if"

    AstNode *node = ast_new(AST_IF, if_tok.line);
    if (!node) return NULL;

    if (!expect(p, TOK_LPAREN, "'(' after 'if'")) { ast_free(node); return NULL; }
    node->cond = parse_condition(p);
    if (!node->cond) { ast_free(node); return NULL; }
    if (!expect(p, TOK_RPAREN, "')' after condition")) { ast_free(node); return NULL; }

    node->body = parse_block(p);
    if (!node->body) { ast_free(node); return NULL; }

    if (check(p, TOK_ELSE)) {
        bump(p);
        node->else_ = parse_block(p);
        if (!node->else_) { ast_free(node); return NULL; }
    }
    return node;
}

// while_stmt := "while" "(" condition ")" block
static AstNode *parse_while(Parser *p) {
    Token while_tok = current(p);
    bump(p); // consume "while"

    AstNode *node = ast_new(AST_WHILE, while_tok.line);
    if (!node) return NULL;

    if (!expect(p, TOK_LPAREN, "'(' after 'while'")) { ast_free(node); return NULL; }
    node->cond = parse_condition(p);
    if (!node->cond) { ast_free(node); return NULL; }
    if (!expect(p, TOK_RPAREN, "')' after condition")) { ast_free(node); return NULL; }

    node->body = parse_block(p);
    if (!node->body) { ast_free(node); return NULL; }
    return node;
}

// block := "{" statement* "}"
static AstNode *parse_block(Parser *p) {
    Token brace = current(p);
    if (!expect(p, TOK_LBRACE, "'{'")) return NULL;

    AstNode *block = ast_new(AST_BLOCK, brace.line);
    if (!block) return NULL;

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        AstNode *stmt = parse_statement(p);
        if (!stmt) { ast_free(block); return NULL; }
        if (!node_add_stmt(block, stmt)) {
            ast_free(stmt);
            ast_free(block);
            return NULL;
        }
        if (p->error[0] != '\0') { ast_free(block); return NULL; }
    }

    if (!expect(p, TOK_RBRACE, "'}'")) { ast_free(block); return NULL; }
    return block;
}

// statement := declaration | assignment | return_stmt | if_stmt | while_stmt | block
static AstNode *parse_statement(Parser *p) {
    switch (current(p).type) {
        case TOK_INT:    return parse_declaration(p);
        case TOK_RETURN: return parse_return(p);
        case TOK_IF:     return parse_if(p);
        case TOK_WHILE:  return parse_while(p);
        case TOK_LBRACE: return parse_block(p);
        case TOK_IDENT:  return parse_assignment(p);
        default:
            set_error(p, &p->current, "expected a statement");
            return NULL;
    }
}

// program := statement* EOF
AstNode *parser_parse(Parser *p) {
    if (p->error[0] != '\0') return NULL;

    AstNode *program = ast_new(AST_PROGRAM, 1);
    if (!program) return NULL;

    while (!check(p, TOK_EOF)) {
        if (current(p).type == TOK_ERROR) {
            set_error(p, &p->current, p->current.text);
            ast_free(program);
            return NULL;
        }
        AstNode *stmt = parse_statement(p);
        if (!stmt) { ast_free(program); return NULL; }
        if (!node_add_stmt(program, stmt)) {
            ast_free(stmt);
            ast_free(program);
            return NULL;
        }
        if (p->error[0] != '\0') { ast_free(program); return NULL; }
    }

    return program;
}

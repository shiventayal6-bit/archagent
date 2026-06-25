// ArchAgent IDE - C-to-A64 Playground: code generator implementation

#include "c2asm_codegen.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// temporary registers map to x16..x25
#define TEMP_REG_BASE 16

bool codegen_init(Codegen *cg) {
    if (!cg) return false;
    memset(cg, 0, sizeof(*cg));
    cg->line_cap = 64;
    cg->lines = malloc(cg->line_cap * sizeof(char *));
    if (!cg->lines) return false;
    cg->line_count = 0;
    cg->had_error = false;
    cg->error[0] = '\0';
    return true;
}

void codegen_free(Codegen *cg) {
    if (!cg) return;
    for (size_t i = 0; i < cg->line_count; i++) {
        free(cg->lines[i]);
    }
    free(cg->lines);
    cg->lines = NULL;
    cg->line_count = 0;
    cg->line_cap = 0;
}

static void set_cg_error(Codegen *cg, int line, const char *msg) {
    if (cg->had_error) return; // keep first error
    cg->had_error = true;
    if (line > 0) {
        snprintf(cg->error, sizeof(cg->error), "line %d: %s", line, msg);
    } else {
        snprintf(cg->error, sizeof(cg->error), "%s", msg);
    }
}

// append one formatted assembly line
static void codegen_emit(Codegen *cg, const char *fmt, ...) {
    if (cg->had_error) return;
    if (cg->line_count >= C2ASM_MAX_ASM_LINES) {
        set_cg_error(cg, 0, "program too large (assembly line limit reached)");
        return;
    }
    if (cg->line_count >= cg->line_cap) {
        size_t new_cap = cg->line_cap * 2;
        char **grown = realloc(cg->lines, new_cap * sizeof(char *));
        if (!grown) { set_cg_error(cg, 0, "out of memory"); return; }
        cg->lines = grown;
        cg->line_cap = new_cap;
    }
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    char *copy = strdup(buf);
    if (!copy) { set_cg_error(cg, 0, "out of memory"); return; }
    cg->lines[cg->line_count++] = copy;
}

// ---- register allocation ----

static int codegen_alloc_temp(Codegen *cg) {
    for (int i = 0; i < C2ASM_MAX_TEMPS; i++) {
        if (!cg->temp_used[i]) {
            cg->temp_used[i] = true;
            return TEMP_REG_BASE + i;
        }
    }
    set_cg_error(cg, 0, "expression too complex (out of temporary registers)");
    return -1;
}

static void codegen_free_temp(Codegen *cg, int reg) {
    if (reg < TEMP_REG_BASE || reg >= TEMP_REG_BASE + C2ASM_MAX_TEMPS) return;
    cg->temp_used[reg - TEMP_REG_BASE] = false;
}

static bool is_temp(int reg) {
    return reg >= TEMP_REG_BASE && reg < TEMP_REG_BASE + C2ASM_MAX_TEMPS;
}

static int codegen_get_var_reg(Codegen *cg, const char *name) {
    for (int i = 0; i < cg->var_count; i++) {
        if (strcmp(cg->vars[i].name, name) == 0) {
            return cg->vars[i].reg;
        }
    }
    return -1;
}

static int codegen_alloc_var(Codegen *cg, const char *name) {
    if (codegen_get_var_reg(cg, name) != -1) {
        char msg[300];
        snprintf(msg, sizeof(msg), "variable '%s' is already declared", name);
        set_cg_error(cg, 0, msg);
        return -1;
    }
    if (cg->var_count >= C2ASM_MAX_VARS) {
        set_cg_error(cg, 0, "too many variables (max 15)");
        return -1;
    }
    int reg = cg->var_count + 1; // x1..x15
    snprintf(cg->vars[cg->var_count].name, sizeof(cg->vars[cg->var_count].name),
             "%s", name);
    cg->vars[cg->var_count].reg = reg;
    cg->var_count++;
    return reg;
}

// emit a movz/movk sequence (plus neg for negatives) loading any 64-bit value
static void codegen_emit_load_imm(Codegen *cg, int reg, int64_t val) {
    if (cg->had_error) return;

    bool negative = val < 0;
    uint64_t mag = negative ? (uint64_t)(-(val + 1)) + 1ULL : (uint64_t) val;

    if (mag == 0) {
        codegen_emit(cg, "movz x%d, #0", reg);
    } else {
        bool first = true;
        for (int shift = 0; shift < 64; shift += 16) {
            uint64_t chunk = (mag >> shift) & 0xFFFFULL;
            if (chunk == 0) continue;
            if (first) {
                if (shift == 0) {
                    codegen_emit(cg, "movz x%d, #%llu",
                                 reg, (unsigned long long) chunk);
                } else {
                    codegen_emit(cg, "movz x%d, #%llu, lsl #%d",
                                 reg, (unsigned long long) chunk, shift);
                }
                first = false;
            } else {
                codegen_emit(cg, "movk x%d, #%llu, lsl #%d",
                             reg, (unsigned long long) chunk, shift);
            }
        }
    }

    if (negative) {
        codegen_emit(cg, "neg x%d, x%d", reg, reg);
    }
}

// ---- expression / statement generation ----

static int  codegen_expr(Codegen *cg, AstNode *node);
static void codegen_stmt(Codegen *cg, AstNode *node);

// returns the register number holding the expression result
static int codegen_expr(Codegen *cg, AstNode *node) {
    if (cg->had_error || !node) return -1;

    switch (node->type) {
        case AST_NUMBER: {
            int reg = codegen_alloc_temp(cg);
            if (reg < 0) return -1;
            codegen_emit_load_imm(cg, reg, node->int_val);
            return reg;
        }
        case AST_IDENT: {
            int reg = codegen_get_var_reg(cg, node->name);
            if (reg < 0) {
                char msg[300];
                snprintf(msg, sizeof(msg),
                         "use of undeclared variable '%s'", node->name);
                set_cg_error(cg, node->line, msg);
                return -1;
            }
            return reg;
        }
        case AST_UNARY: {
            int sub = codegen_expr(cg, node->left);
            if (sub < 0) return -1;
            int dst = codegen_alloc_temp(cg);
            if (dst < 0) { codegen_free_temp(cg, sub); return -1; }
            codegen_emit(cg, "neg x%d, x%d", dst, sub);
            if (is_temp(sub)) codegen_free_temp(cg, sub);
            return dst;
        }
        case AST_BINOP: {
            if (strcmp(node->op, "/") == 0) {
                set_cg_error(cg, node->line,
                             "Division is not supported by the A64 subset.");
                return -1;
            }
            int left = codegen_expr(cg, node->left);
            if (left < 0) return -1;
            int right = codegen_expr(cg, node->right);
            if (right < 0) { if (is_temp(left)) codegen_free_temp(cg, left); return -1; }

            const char *mnemonic = NULL;
            if      (strcmp(node->op, "+") == 0) mnemonic = "add";
            else if (strcmp(node->op, "-") == 0) mnemonic = "sub";
            else if (strcmp(node->op, "*") == 0) mnemonic = "mul";
            else {
                set_cg_error(cg, node->line, "unsupported binary operator");
                if (is_temp(left))  codegen_free_temp(cg, left);
                if (is_temp(right)) codegen_free_temp(cg, right);
                return -1;
            }

            int dst = codegen_alloc_temp(cg);
            if (dst < 0) {
                if (is_temp(left))  codegen_free_temp(cg, left);
                if (is_temp(right)) codegen_free_temp(cg, right);
                return -1;
            }
            codegen_emit(cg, "%s x%d, x%d, x%d", mnemonic, dst, left, right);
            if (is_temp(left))  codegen_free_temp(cg, left);
            if (is_temp(right)) codegen_free_temp(cg, right);
            return dst;
        }
        default:
            set_cg_error(cg, node->line, "unexpected node in expression");
            return -1;
    }
}

// emit the inverse-condition compare+branch that skips a body when the
// source condition is false. branches to label_false.
static void codegen_cond_branch(Codegen *cg, AstNode *cond, const char *label_false) {
    if (cg->had_error || !cond) return;
    if (cond->type != AST_COND) {
        set_cg_error(cg, cond ? cond->line : 0, "invalid condition");
        return;
    }

    int left = codegen_expr(cg, cond->left);
    if (left < 0) return;
    int right = codegen_expr(cg, cond->right);
    if (right < 0) { if (is_temp(left)) codegen_free_temp(cg, left); return; }

    codegen_emit(cg, "cmp x%d, x%d", left, right);
    if (is_temp(left))  codegen_free_temp(cg, left);
    if (is_temp(right)) codegen_free_temp(cg, right);

    // inverse condition: branch away when the source condition is FALSE
    const char *branch = NULL;
    if      (strcmp(cond->op, "==") == 0) branch = "b.ne";
    else if (strcmp(cond->op, "!=") == 0) branch = "b.eq";
    else if (strcmp(cond->op, "<")  == 0) branch = "b.ge";
    else if (strcmp(cond->op, "<=") == 0) branch = "b.gt";
    else if (strcmp(cond->op, ">")  == 0) branch = "b.le";
    else if (strcmp(cond->op, ">=") == 0) branch = "b.lt";
    else { set_cg_error(cg, cond->line, "unsupported comparison operator"); return; }

    codegen_emit(cg, "%s %s", branch, label_false);
}

static void codegen_stmt(Codegen *cg, AstNode *node) {
    if (cg->had_error || !node) return;

    switch (node->type) {
        case AST_DECL: {
            int var_reg = codegen_alloc_var(cg, node->name);
            if (var_reg < 0) return;
            if (node->left) {
                int result = codegen_expr(cg, node->left);
                if (result < 0) return;
                if (result != var_reg) {
                    codegen_emit(cg, "mov x%d, x%d", var_reg, result);
                }
                if (is_temp(result)) codegen_free_temp(cg, result);
            } else {
                codegen_emit(cg, "movz x%d, #0", var_reg);
            }
            break;
        }
        case AST_ASSIGN: {
            int var_reg = codegen_get_var_reg(cg, node->name);
            if (var_reg < 0) {
                char msg[300];
                snprintf(msg, sizeof(msg),
                         "assignment to undeclared variable '%s'", node->name);
                set_cg_error(cg, node->line, msg);
                return;
            }
            int result = codegen_expr(cg, node->left);
            if (result < 0) return;
            if (result != var_reg) {
                codegen_emit(cg, "mov x%d, x%d", var_reg, result);
            }
            if (is_temp(result)) codegen_free_temp(cg, result);
            break;
        }
        case AST_RETURN: {
            int result = codegen_expr(cg, node->left);
            if (result < 0) return;
            if (result != 0) {
                codegen_emit(cg, "mov x0, x%d", result);
            }
            if (is_temp(result)) codegen_free_temp(cg, result);
            codegen_emit(cg, "and x0, x0, x0");
            break;
        }
        case AST_IF: {
            int id = cg->label_counter++;
            char end_label[32];
            snprintf(end_label, sizeof(end_label), "lif%dend", id);

            if (node->else_) {
                char else_label[32];
                snprintf(else_label, sizeof(else_label), "lif%delse", id);
                codegen_cond_branch(cg, node->cond, else_label);
                codegen_stmt(cg, node->body);
                codegen_emit(cg, "b %s", end_label);
                codegen_emit(cg, "%s:", else_label);
                codegen_stmt(cg, node->else_);
                codegen_emit(cg, "%s:", end_label);
            } else {
                codegen_cond_branch(cg, node->cond, end_label);
                codegen_stmt(cg, node->body);
                codegen_emit(cg, "%s:", end_label);
            }
            break;
        }
        case AST_WHILE: {
            int id = cg->label_counter++;
            char start_label[32];
            char end_label[32];
            snprintf(start_label, sizeof(start_label), "lwhile%dstart", id);
            snprintf(end_label, sizeof(end_label), "lwhile%dend", id);

            codegen_emit(cg, "%s:", start_label);
            codegen_cond_branch(cg, node->cond, end_label);
            codegen_stmt(cg, node->body);
            codegen_emit(cg, "b %s", start_label);
            codegen_emit(cg, "%s:", end_label);
            break;
        }
        case AST_BLOCK: {
            for (size_t i = 0; i < node->stmt_count; i++) {
                codegen_stmt(cg, node->stmts[i]);
                if (cg->had_error) return;
            }
            break;
        }
        default:
            set_cg_error(cg, node->line, "unexpected statement node");
            break;
    }
}

bool codegen_generate(Codegen *cg, AstNode *program) {
    if (!cg || !program || program->type != AST_PROGRAM) {
        if (cg) set_cg_error(cg, 0, "invalid program");
        return false;
    }

    bool saw_return = false;
    for (size_t i = 0; i < program->stmt_count; i++) {
        codegen_stmt(cg, program->stmts[i]);
        if (cg->had_error) return false;
        if (program->stmts[i]->type == AST_RETURN) saw_return = true;
    }

    // ensure the program halts even without an explicit top-level return
    if (!saw_return) {
        codegen_emit(cg, "movz x0, #0");
        codegen_emit(cg, "and x0, x0, x0");
    }

    return !cg->had_error;
}

char *codegen_get_assembly(const Codegen *cg) {
    if (!cg) return NULL;

    size_t total = 1; // trailing NUL
    for (size_t i = 0; i < cg->line_count; i++) {
        total += strlen(cg->lines[i]) + 1; // + newline
    }

    char *out = malloc(total);
    if (!out) return NULL;
    out[0] = '\0';

    size_t off = 0;
    for (size_t i = 0; i < cg->line_count; i++) {
        size_t len = strlen(cg->lines[i]);
        memcpy(out + off, cg->lines[i], len);
        off += len;
        out[off++] = '\n';
    }
    out[off] = '\0';
    return out;
}

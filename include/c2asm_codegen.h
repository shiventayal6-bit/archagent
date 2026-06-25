// ArchAgent IDE - C-to-A64 Playground: code generator
// Lowers the AST to A64 assembly text accepted by the coursework assembler.

#ifndef ARCHAGENT_C2ASM_CODEGEN_H
#define ARCHAGENT_C2ASM_CODEGEN_H

#include "c2asm_parser.h"

#include <stdbool.h>
#include <stddef.h>

#define C2ASM_MAX_VARS       15
#define C2ASM_MAX_TEMPS      10   // x16-x25
#define C2ASM_MAX_ASM_LINES  10000

typedef struct {
    char  name[256];
    int   reg;   // 1..15 (maps to x1..x15)
} VarEntry;

typedef struct {
    VarEntry vars[C2ASM_MAX_VARS];
    int      var_count;

    bool     temp_used[C2ASM_MAX_TEMPS];  // x16..x25

    char   **lines;          // generated assembly lines
    size_t   line_count;
    size_t   line_cap;

    int      label_counter;  // for unique label generation

    char     error[512];
    bool     had_error;
} Codegen;

bool codegen_init(Codegen *cg);
void codegen_free(Codegen *cg);
bool codegen_generate(Codegen *cg, AstNode *program);
char *codegen_get_assembly(const Codegen *cg);  // joins lines with \n, caller frees

#endif // ARCHAGENT_C2ASM_CODEGEN_H

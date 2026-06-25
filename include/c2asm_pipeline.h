// ArchAgent IDE - C-to-A64 Playground: pipeline
// Drives lex -> parse -> codegen -> assemble -> emulate and captures artifacts.

#ifndef ARCHAGENT_C2ASM_PIPELINE_H
#define ARCHAGENT_C2ASM_PIPELINE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *source_text;      // inline source (NULL if using file)
    const char *source_file;      // path to .cmini file (NULL if inline)
    const char *assemble_bin;     // path to assembler (NULL = auto-detect)
    const char *emulate_bin;      // path to emulator (NULL = auto-detect)
    const char *repo_root;        // repo root for auto-detect (NULL = try defaults)
    const char *session_base_dir; // where to create sessions (NULL = ./.archagent)
    int         timeout_seconds;  // default 10
    bool        emit_asm_only;    // stop after codegen, no assemble/emulate
    bool        keep_artifacts;   // keep session files (default true)
    bool        verbose;
} C2AsmOptions;

typedef struct {
    bool    ok;
    char    stage[32];    // "lex", "parse", "codegen", "assemble", "emulate", "result_parse", "internal"
    char    error[1024];

    char    session_id[64];
    char    session_dir[4096];

    char    input_path[4096];
    char    assembly_path[4096];
    char    binary_path[4096];
    char    emulator_output_path[4096];
    char    result_json_path[4096];

    char   *generated_assembly;   // malloc'd
    char   *emulator_output;      // malloc'd

    uint64_t return_value;
    bool     return_value_available;
} C2AsmResult;

bool c2asm_pipeline_run(const C2AsmOptions *opts, C2AsmResult *out);
void c2asm_result_free(C2AsmResult *result);
void c2asm_result_print_json(const C2AsmResult *result, const C2AsmOptions *opts);

#endif // ARCHAGENT_C2ASM_PIPELINE_H

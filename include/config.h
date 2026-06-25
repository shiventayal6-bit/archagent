// ArchAgent — CLI configuration
// All runtime parameters parsed from argv, with documented defaults.

#ifndef ARCHAGENT_CONFIG_H
#define ARCHAGENT_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    const char *project_path;      // --project <path>
    const char *request_text;      // --request <text>
    const char *request_file;      // --request-file <path>
    const char *backend;           // --backend mock|llama|ollama
    const char *model;             // --model <name>
    const char *build_cmd;         // --build-cmd <cmd>
    const char *test_cmd;          // --test-cmd <cmd>
    const char *bench_cmd;         // --bench-cmd <cmd>
    const char *audit_dir;         // --audit-dir <path>
    int         timeout_seconds;   // --timeout <seconds>
    size_t      max_context_bytes; // --max-context-bytes <n>
    bool        apply_to_original; // --apply
    bool        assume_yes;        // --yes
    bool        verbose;           // --verbose
    bool        profile_only;      // --profile
    bool        scan_only;         // --scan
    bool        json;              // --json

    // C2ASM mode fields
    bool        c2asm_mode;          // --c2asm or --c2asm-code
    const char *c2asm_file;          // --c2asm <file>
    const char *c2asm_code;          // --c2asm-code "<inline>"
    const char *c2asm_assemble_bin;  // --assemble-bin <path>
    const char *c2asm_emulate_bin;   // --emulate-bin <path>
    const char *c2asm_repo_root;     // --repo-root <path>
    bool        c2asm_emit_asm_only; // --emit-asm-only
    bool        c2asm_keep_artifacts;// --keep-artifacts (default true)
} Config;

// Fill config with default values
void config_set_defaults(Config *cfg);

// Parse command line arguments into config
// Returns true on success, false on error
bool config_parse(int argc, char **argv, Config *cfg);

// Print usage information
void config_print_usage(const char *program_name);

#endif // ARCHAGENT_CONFIG_H

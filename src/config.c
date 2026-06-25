// Created by st3125 on 2026/6/11
// ArchAgent IDE - Configuration parsing

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Fill config with default values as specified in Section 1.1 of the spec
void config_set_defaults(Config *cfg) {
    cfg->project_path      = NULL;
    cfg->request_text      = NULL;
    cfg->request_file      = NULL;
    cfg->backend           = "mock";
    cfg->model             = NULL;
    cfg->build_cmd         = "make";
    cfg->test_cmd          = "make test";
    cfg->bench_cmd         = NULL;
    cfg->audit_dir         = ".archagent";
    cfg->timeout_seconds   = 10;
    cfg->max_context_bytes = 30000;
    cfg->apply_to_original = false;
    cfg->assume_yes        = false;
    cfg->verbose           = false;
    cfg->profile_only      = false;
    cfg->scan_only         = false;
    cfg->json              = false;

    cfg->c2asm_mode          = false;
    cfg->c2asm_file          = NULL;
    cfg->c2asm_code          = NULL;
    cfg->c2asm_assemble_bin  = NULL;
    cfg->c2asm_emulate_bin   = NULL;
    cfg->c2asm_repo_root     = NULL;
    cfg->c2asm_emit_asm_only = false;
    cfg->c2asm_keep_artifacts= true;
}

void config_print_usage(const char *program_name) {
    printf("Usage: %s [options]\n\n", program_name);
    printf("Options:\n");
    printf("  --help                    Print this help and exit\n");
    printf("  --version                 Print version and exit\n");
    printf("  --profile                 Generate target profile\n");
    printf("  --scan                    Scan project files\n");
    printf("  --json                    Emit JSON output\n");
    printf("  --project <path>          Path to project\n");
    printf("  --request <text>          User request\n");
    printf("  --request-file <path>     Read request from file\n");
    printf("  --backend <name>          Backend: mock|llama|ollama\n");
    printf("  --model <name>            Model name or path\n");
    printf("  --build-cmd <cmd>         Build command (default: make)\n");
    printf("  --test-cmd <cmd>          Test command (default: make test)\n");
    printf("  --bench-cmd <cmd>         Benchmark command\n");
    printf("  --apply                   Apply patch to original project\n");
    printf("  --no-apply                Do not apply patch (default)\n");
    printf("  --yes                     Assume yes for confirmations\n");
    printf("  --timeout <seconds>       Timeout in seconds (default: 10)\n");
    printf("  --max-context-bytes <n>   Max context bytes (default: 30000)\n");
    printf("  --audit-dir <path>        Audit directory (default: .archagent)\n");
    printf("  --verbose                 Print extra information\n");
    printf("  --c2asm <file>            Compile and run a .cmini file\n");
    printf("  --c2asm-code \"<src>\"      Compile and run inline C-Mini source\n");
    printf("  --emit-asm-only           Stop after code generation, print assembly\n");
    printf("  --assemble-bin <path>     Path to assemble binary\n");
    printf("  --emulate-bin <path>      Path to emulate binary\n");
    printf("  --repo-root <path>        Repo root for auto-detecting tools\n");
    printf("  --keep-artifacts          Keep session files after run (default: on)\n");
}

// Parse command line arguments into config
// Returns true on success, false on error
bool config_parse(int argc, char **argv, Config *cfg) {
    // start with defaults
    config_set_defaults(cfg);

    for (int i = 1; i < argc; i++) {
        // flags with no value
        if (strcmp(argv[i], "--help") == 0) {
            config_print_usage(argv[0]);
            exit(0);
        }
        if (strcmp(argv[i], "--version") == 0) {
            printf("archagent 1.0.0\n");
            exit(0);
        }
        if (strcmp(argv[i], "--profile") == 0) {
            cfg->profile_only = true;
            continue;
        }
        if (strcmp(argv[i], "--scan") == 0) {
            cfg->scan_only = true;
            continue;
        }
        if (strcmp(argv[i], "--json") == 0) {
            cfg->json = true;
            continue;
        }
        if (strcmp(argv[i], "--apply") == 0) {
            cfg->apply_to_original = true;
            continue;
        }
        if (strcmp(argv[i], "--no-apply") == 0) {
            cfg->apply_to_original = false;
            continue;
        }
        if (strcmp(argv[i], "--yes") == 0) {
            cfg->assume_yes = true;
            continue;
        }
        if (strcmp(argv[i], "--verbose") == 0) {
            cfg->verbose = true;
            continue;
        }
        if (strcmp(argv[i], "--emit-asm-only") == 0) {
            cfg->c2asm_emit_asm_only = true;
            continue;
        }
        if (strcmp(argv[i], "--keep-artifacts") == 0) {
            cfg->c2asm_keep_artifacts = true;
            continue;
        }

        // flags that take a value — check next argument exists
        if (i + 1 >= argc) {
            fprintf(stderr, "Error: %s requires a value\n", argv[i]);
            return false;
        }

        if (strcmp(argv[i], "--project") == 0) {
            cfg->project_path = argv[++i];
        } else if (strcmp(argv[i], "--request") == 0) {
            cfg->request_text = argv[++i];
        } else if (strcmp(argv[i], "--request-file") == 0) {
            cfg->request_file = argv[++i];
        } else if (strcmp(argv[i], "--backend") == 0) {
            cfg->backend = argv[++i];
        } else if (strcmp(argv[i], "--model") == 0) {
            cfg->model = argv[++i];
        } else if (strcmp(argv[i], "--build-cmd") == 0) {
            cfg->build_cmd = argv[++i];
        } else if (strcmp(argv[i], "--test-cmd") == 0) {
            cfg->test_cmd = argv[++i];
        } else if (strcmp(argv[i], "--bench-cmd") == 0) {
            cfg->bench_cmd = argv[++i];
        } else if (strcmp(argv[i], "--audit-dir") == 0) {
            cfg->audit_dir = argv[++i];
        } else if (strcmp(argv[i], "--timeout") == 0) {
            cfg->timeout_seconds = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--max-context-bytes") == 0) {
            cfg->max_context_bytes = (size_t) atoi(argv[++i]);
        } else if (strcmp(argv[i], "--c2asm") == 0) {
            cfg->c2asm_file = argv[++i];
            cfg->c2asm_mode = true;
        } else if (strcmp(argv[i], "--c2asm-code") == 0) {
            cfg->c2asm_code = argv[++i];
            cfg->c2asm_mode = true;
        } else if (strcmp(argv[i], "--assemble-bin") == 0) {
            cfg->c2asm_assemble_bin = argv[++i];
        } else if (strcmp(argv[i], "--emulate-bin") == 0) {
            cfg->c2asm_emulate_bin = argv[++i];
        } else if (strcmp(argv[i], "--repo-root") == 0) {
            cfg->c2asm_repo_root = argv[++i];
        } else {
            fprintf(stderr, "Error: unknown option: %s\n", argv[i]);
            return false;
        }
    }

    // validate: --request and --request-file cannot both be set
    if (cfg->request_text != NULL && cfg->request_file != NULL) {
        fprintf(stderr, "Error: --request and --request-file cannot both be provided\n");
        return false;
    }

    // validate: --c2asm and --c2asm-code cannot both be set
    if (cfg->c2asm_file != NULL && cfg->c2asm_code != NULL) {
        fprintf(stderr, "Error: --c2asm and --c2asm-code cannot both be provided\n");
        return false;
    }

    // validate: scan needs a project
    if (cfg->scan_only && cfg->project_path == NULL) {
        fprintf(stderr, "Error: --scan requires --project\n");
        return false;
    }

    // validate: request run needs a project and a request
    if (cfg->request_text != NULL || cfg->request_file != NULL) {
        if (cfg->project_path == NULL) {
            fprintf(stderr, "Error: --request requires --project\n");
            return false;
        }
    }

    return true;
}

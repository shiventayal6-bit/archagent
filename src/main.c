// Created by st3125 on 2026/6/11
// Modified by sm4925 on 2026/6/14
// ArchAgent IDE - Main entry point

#include "config.h"
#include "target_profile.h"
#include "project_scanner.h"
#include "context_packer.h"
#include "prompt_builder.h"
#include "mock_backend.h"
#include "llama_backend.h"
#include "ollama_backend.h"
#include "response_parser.h"
#include "diff_parser.h"
#include "patch_validator.h"
#include "sandbox.h"
#include "patch_applier.h"
#include "command_runner.h"
#include "audit_logger.h"
#include "c2asm_pipeline.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- helpers for the --profile and --scan modes (already working) ---

static int run_profile(const Config *cfg) {
    TargetProfile profile;
    if (!target_profile_build(&profile)) {
        fprintf(stderr, "Error: failed to build target profile\n");
        return 1;
    }
    if (cfg->json) {
        printf("{\n  \"ok\": true,\n  \"profile_text\": \"");
        for (char *c = profile.text; *c; c++) {
            if (*c == '\n')      printf("\\n");
            else if (*c == '"') printf("\\\"");
            else                putchar(*c);
        }
        printf("\"\n}\n");
    } else {
        printf("%s\n", profile.text);
    }
    target_profile_free(&profile);
    return 0;
}

static int run_scan(const Config *cfg) {
    ProjectIndex index;
    if (!project_scan(cfg->project_path, &index)) {
        fprintf(stderr, "Error: failed to scan project: %s\n", cfg->project_path);
        return 1;
    }
    if (cfg->json) {
        printf("{\n");
        printf("  \"ok\": true,\n");
        printf("  \"project\": \"%s\",\n", cfg->project_path);
        printf("  \"files\": [\n");
        for (size_t i = 0; i < index.count; i++) {
            ProjectFile *f = &index.files[i];
            const char *kind = f->is_source   ? "source"
                             : f->is_header   ? "header"
                             : f->is_makefile ? "makefile"
                             : f->is_test     ? "test"
                             :                  "other";
            printf("    {\"path\": \"%s\", \"size\": %zu, \"kind\": \"%s\"}%s\n",
                   f->relative_path, f->size_bytes, kind,
                   i + 1 < index.count ? "," : "");
        }
        printf("  ]\n}\n");
    } else {
        printf("Project: %s\n", cfg->project_path);
        printf("Files found: %zu\n\n", index.count);
        for (size_t i = 0; i < index.count; i++) {
            ProjectFile *f = &index.files[i];
            printf("  %-40s  %6zu bytes\n", f->relative_path, f->size_bytes);
        }
    }
    project_index_free(&index);
    return 0;
}

// --- print a result.json that matches the format the GUI expects ---

static void write_result_json(const Sandbox *sandbox, const Config *cfg,
                              bool patch_validated, bool sandbox_created,
                              bool build_ran, int build_exit, bool build_passed,
                              bool tests_ran, int test_exit, bool tests_passed,
                              bool bench_ran,
                              const char *summary) {
    char buf[8192];
    int n = snprintf(buf, sizeof(buf),
        "{\n"
        "  \"ok\": %s,\n"
        "  \"session_id\": \"%s\",\n"
        "  \"project\": \"%s\",\n"
        "  \"backend\": \"%s\",\n"
        "  \"patch_validated\": %s,\n"
        "  \"sandbox_created\": %s,\n"
        "  \"build\": {\"ran\": %s, \"exit_code\": %d, \"passed\": %s},\n"
        "  \"tests\": {\"ran\": %s, \"exit_code\": %d, \"passed\": %s},\n"
        "  \"benchmark\": {\"ran\": %s},\n"
        "  \"session_dir\": \"%s\",\n"
        "  \"summary\": \"%s\"\n"
        "}\n",
        (patch_validated && build_passed && tests_passed) ? "true" : "false",
        sandbox->session_id,
        cfg->project_path,
        cfg->backend,
        patch_validated ? "true" : "false",
        sandbox_created ? "true" : "false",
        build_ran ? "true" : "false", build_exit, build_passed ? "true" : "false",
        tests_ran ? "true" : "false", test_exit, tests_passed ? "true" : "false",
        bench_ran ? "true" : "false",
        sandbox->session_dir,
        summary
    );
    if (n > 0) {
        audit_write_file(sandbox, "result.json", buf);
    }
}

// --- report a backend that failed or is unavailable ---
// this is an expected/handled condition, not a crash, so it returns 0
static int backend_error(Sandbox *sandbox, const Config *cfg, const char *message) {
    audit_write_file(sandbox, "model_response.txt", message);
    write_result_json(sandbox, cfg, false, true,
                      false, 0, false, false, 0, false, false,
                      message);
    if (cfg->json) {
        printf("{\n  \"ok\": false,\n  \"session_id\": \"%s\",\n"
               "  \"summary\": \"%s\"\n}\n",
               sandbox->session_id, message);
    } else {
        printf("Backend error: %s\n", message);
    }
    return 0;
}

// --- the full pipeline for --request ---

static int run_request(const Config *cfg) {
    // step 1: config already parsed

    // step 2: create audit session
    Sandbox sandbox;
    if (!sandbox_create(cfg->project_path, cfg->audit_dir, &sandbox)) {
        fprintf(stderr, "Error: failed to create sandbox session\n");
        return 1;
    }
    audit_log_event(&sandbox, "{\"event\":\"session_start\"}");

    // step 3: generate target profile
    TargetProfile profile;
    if (!target_profile_build(&profile)) {
        fprintf(stderr, "Error: failed to build target profile\n");
        return 1;
    }
    audit_write_file(&sandbox, "profile.txt", profile.text);
    audit_log_event(&sandbox, "{\"event\":\"profile_generated\"}");

    // step 4: scan project
    ProjectIndex index;
    if (!project_scan(cfg->project_path, &index)) {
        fprintf(stderr, "Error: failed to scan project\n");
        return 1;
    }
    {
        char idx_text[16384];
        size_t used = 0;
        for (size_t i = 0; i < index.count && used < sizeof(idx_text) - 256; i++) {
            int w = snprintf(idx_text + used, sizeof(idx_text) - used,
                             "%s (%zu bytes)\n",
                             index.files[i].relative_path,
                             index.files[i].size_bytes);
            if (w > 0) used += (size_t) w;
        }
        audit_write_file(&sandbox, "project_index.txt", idx_text);
    }
    audit_log_event(&sandbox, "{\"event\":\"project_scanned\"}");

    // get the request text (either --request or --request-file)
    char request_buf[8192];
    const char *request = cfg->request_text;
    if (!request && cfg->request_file) {
        FILE *f = fopen(cfg->request_file, "r");
        if (!f) {
            fprintf(stderr, "Error: failed to open request file\n");
            return 1;
        }
        size_t n = fread(request_buf, 1, sizeof(request_buf) - 1, f);
        request_buf[n] = '\0';
        fclose(f);
        request = request_buf;
    }

    // step 5: pack context
    ContextPack context;
    if (!context_pack_build(cfg->project_path, &index, request,
                            cfg->max_context_bytes, &context)) {
        fprintf(stderr, "Error: failed to pack context\n");
        return 1;
    }

    // step 6: build prompt
    Prompt prompt;
    if (!prompt_build(&profile, &context, request, &prompt)) {
        fprintf(stderr, "Error: failed to build prompt\n");
        return 1;
    }
    audit_write_file(&sandbox, "prompt.txt", prompt.text);

    // step 7: call backend
    ModelResponse response;
    if (strcmp(cfg->backend, "mock") == 0) {
        if (!mock_backend_generate(request, &response)) {
            fprintf(stderr, "Error: mock backend failed\n");
            return 1;
        }
    } else if (strcmp(cfg->backend, "llama") == 0) {
        if (!llama_backend_generate(prompt.text, cfg->model, cfg->timeout_seconds, &response)) {
            const char *message;
            char message_buf[256];
            if (response.exit_code == 127) {
                message = "llama-cli not found. Install llama.cpp to use this backend. "
                          "See README.md for instructions.";
            } else if (response.exit_code == 2) {
                message = "No model path provided. Use --model <path-to-gguf>";
            } else if (response.timed_out) {
                message = "llama backend timed out";
            } else {
                snprintf(message_buf, sizeof(message_buf),
                        "llama backend exited with code %d", response.exit_code);
                message = message_buf;
            }
            return backend_error(&sandbox, cfg, message);
        }
    } else if (strcmp(cfg->backend, "ollama") == 0) {
        if (!ollama_backend_generate(prompt.text,
                                     cfg->model,
                                     cfg->timeout_seconds,
                                     &response)) {
            const char *message =
                response.text ? response.text : "ollama backend failed";

            int rc = backend_error(&sandbox, cfg, message);
            model_response_free(&response);
            return rc;
        }
    } else {
        char message_buf[128];
        snprintf(message_buf, sizeof(message_buf),
                "Unknown backend '%s'. Supported backends are mock, llama, ollama.",
                cfg->backend);
        return backend_error(&sandbox, cfg, message_buf);
    }
    audit_write_file(&sandbox, "model_response.txt", response.text);
    audit_log_event(&sandbox, "{\"event\":\"model_response_received\"}");

    // step 8: parse response
    ParsedResponse parsed;
    if (!response_parse(response.text, &parsed)) {
        audit_write_file(&sandbox, "summary.txt",
                         "Model response could not be parsed.\n");
        write_result_json(&sandbox, cfg, false, true,
                          false, 0, false, false, 0, false, false,
                          "Model response could not be parsed");
        printf("{\"ok\": false, \"session_id\": \"%s\", "
               "\"summary\": \"Model response could not be parsed\"}\n",
               sandbox.session_id);
        return 0;
    }
    audit_write_file(&sandbox, "parsed_plan.txt", parsed.plan_text);
    audit_write_file(&sandbox, "patch.diff", parsed.patch_text);

    // step 9: parse diff
    ParsedDiff diff;
    if (!diff_parse(parsed.patch_text, &diff)) {
        audit_write_file(&sandbox, "validation_report.txt",
                         "Diff could not be parsed.\n");
        write_result_json(&sandbox, cfg, false, true,
                          false, 0, false, false, 0, false, false,
                          "Diff could not be parsed");
        printf("{\"ok\": false, \"session_id\": \"%s\", "
               "\"patch_validated\": false, "
               "\"summary\": \"Diff could not be parsed\"}\n",
               sandbox.session_id);
        return 0;
    }

    // step 10: validate patch
    ValidationReport validation;
    bool patch_validated = patch_validate(&diff, cfg->project_path, &validation);
    audit_write_file(&sandbox, "validation_report.txt", validation.message);
    {
        char event[1280];
        snprintf(event, sizeof(event),
                "{\"event\":\"patch_validated\",\"ok\":%s}",
                patch_validated ? "true" : "false");
        audit_log_event(&sandbox, event);
    }

    if (!patch_validated) {
        write_result_json(&sandbox, cfg, false, true,
                          false, 0, false, false, 0, false, false,
                          validation.message);
        if (cfg->json) {
            printf("{\n  \"ok\": false,\n  \"session_id\": \"%s\",\n"
                   "  \"patch_validated\": false,\n"
                   "  \"summary\": \"%s\"\n}\n",
                   sandbox.session_id, validation.message);
        } else {
            printf("Patch rejected: %s\n", validation.message);
        }
        return 0;
    }

    // step 11: copy project to sandbox
    if (!sandbox_copy_project(cfg->project_path, &sandbox)) {
        fprintf(stderr, "Error: failed to copy project to sandbox\n");
        return 1;
    }
    audit_log_event(&sandbox, "{\"event\":\"sandbox_created\"}");

    // step 12: apply patch in sandbox
    PatchApplyReport apply_report;
    bool patch_applied = patch_apply_to_sandbox(&diff, sandbox.sandbox_root, &apply_report);
    if (!patch_applied) {
        write_result_json(&sandbox, cfg, true, true,
                          false, 0, false, false, 0, false, false,
                          apply_report.message);
        if (cfg->json) {
            printf("{\n  \"ok\": false,\n  \"session_id\": \"%s\",\n"
                   "  \"patch_validated\": true,\n"
                   "  \"sandbox_created\": true,\n"
                   "  \"summary\": \"%s\"\n}\n",
                   sandbox.session_id, apply_report.message);
        } else {
            printf("Patch application failed: %s\n", apply_report.message);
        }
        return 0;
    }

    // step 13: run build command
    CommandResult build_result;
    bool build_ran = command_run_checked(sandbox.sandbox_root, cfg->build_cmd,
                                         cfg->timeout_seconds, &build_result);
    bool build_passed = build_ran && build_result.exit_code == 0;
    audit_write_file(&sandbox, "build_stdout.txt", build_result.stdout_text ? build_result.stdout_text : "");
    audit_write_file(&sandbox, "build_stderr.txt", build_result.stderr_text ? build_result.stderr_text : "");
    {
        char event[256];
        snprintf(event, sizeof(event),
                "{\"event\":\"build_finished\",\"exit_code\":%d}",
                build_result.exit_code);
        audit_log_event(&sandbox, event);
    }

    // step 14: run test command (only if build passed)
    CommandResult test_result;
    memset(&test_result, 0, sizeof(test_result));
    bool tests_ran = false, tests_passed = false;
    if (build_passed) {
        tests_ran = command_run_checked(sandbox.sandbox_root, cfg->test_cmd,
                                        cfg->timeout_seconds, &test_result);
        tests_passed = tests_ran && test_result.exit_code == 0;
        audit_write_file(&sandbox, "test_stdout.txt", test_result.stdout_text ? test_result.stdout_text : "");
        audit_write_file(&sandbox, "test_stderr.txt", test_result.stderr_text ? test_result.stderr_text : "");
        char event[256];
        snprintf(event, sizeof(event),
                "{\"event\":\"tests_finished\",\"exit_code\":%d}",
                test_result.exit_code);
        audit_log_event(&sandbox, event);
    } else {
        audit_write_file(&sandbox, "test_stdout.txt", "");
        audit_write_file(&sandbox, "test_stderr.txt", "Build failed - tests were not run.\n");
    }

    // step 15: run benchmark command if provided
    bool bench_ran = false;
    if (cfg->bench_cmd && build_passed) {
        CommandResult bench_result;
        bench_ran = command_run_checked(sandbox.sandbox_root, cfg->bench_cmd,
                                        cfg->timeout_seconds, &bench_result);
        audit_write_file(&sandbox, "benchmark.txt",
                         bench_result.stdout_text ? bench_result.stdout_text : "");
        command_result_free(&bench_result);
    } else {
        audit_write_file(&sandbox, "benchmark.txt", "No benchmark command provided.\n");
    }

    // step 16: write result.json and summary.txt
    char summary[512];
    snprintf(summary, sizeof(summary),
            "Patch validated, applied in sandbox, build %s and tests %s.",
            build_passed ? "passed" : "failed",
            tests_passed ? "passed" : (build_passed ? "failed" : "not run"));
    audit_write_file(&sandbox, "summary.txt", summary);

    write_result_json(&sandbox, cfg, true, true,
                      build_ran, build_result.exit_code, build_passed,
                      tests_ran, test_result.exit_code, tests_passed,
                      bench_ran, summary);

    // step 17: print result
    if (cfg->json) {
        printf("{\n"
               "  \"ok\": %s,\n"
               "  \"session_id\": \"%s\",\n"
               "  \"project\": \"%s\",\n"
               "  \"backend\": \"%s\",\n"
               "  \"patch_validated\": true,\n"
               "  \"sandbox_created\": true,\n"
               "  \"build\": {\"ran\": %s, \"exit_code\": %d, \"passed\": %s},\n"
               "  \"tests\": {\"ran\": %s, \"exit_code\": %d, \"passed\": %s},\n"
               "  \"benchmark\": {\"ran\": %s},\n"
               "  \"session_dir\": \"%s\",\n"
               "  \"summary\": \"%s\"\n"
               "}\n",
               (build_passed && tests_passed) ? "true" : "false",
               sandbox.session_id, cfg->project_path, cfg->backend,
               build_ran ? "true" : "false", build_result.exit_code, build_passed ? "true" : "false",
               tests_ran ? "true" : "false", test_result.exit_code, tests_passed ? "true" : "false",
               bench_ran ? "true" : "false",
               sandbox.session_dir, summary);
    } else {
        printf("%s\n", summary);
        printf("Session: %s\n", sandbox.session_dir);
    }

    // cleanup
    target_profile_free(&profile);
    project_index_free(&index);
    context_pack_free(&context);
    prompt_free(&prompt);
    model_response_free(&response);
    parsed_response_free(&parsed);
    parsed_diff_free(&diff);
    command_result_free(&build_result);
    command_result_free(&test_result);

    return 0;
}

// --- the C-to-A64 Playground pipeline for --c2asm / --c2asm-code ---

static int run_c2asm(const Config *cfg) {
    C2AsmOptions opts = {0};
    opts.source_text     = cfg->c2asm_code;
    opts.source_file     = cfg->c2asm_file;
    opts.assemble_bin    = cfg->c2asm_assemble_bin;
    opts.emulate_bin     = cfg->c2asm_emulate_bin;
    opts.repo_root       = cfg->c2asm_repo_root;
    opts.session_base_dir = cfg->audit_dir;
    opts.timeout_seconds = cfg->timeout_seconds;
    opts.emit_asm_only   = cfg->c2asm_emit_asm_only;
    opts.keep_artifacts  = cfg->c2asm_keep_artifacts;
    opts.verbose         = cfg->verbose;

    C2AsmResult result;
    bool ok = c2asm_pipeline_run(&opts, &result);
    (void) ok;

    if (cfg->json) {
        c2asm_result_print_json(&result, &opts);
    } else {
        if (result.ok) {
            if (opts.emit_asm_only) {
                printf("%s\n", result.generated_assembly ? result.generated_assembly : "");
            } else {
                printf("Return value (X00): %" PRIu64 " (0x%016" PRIX64 ")\n",
                       result.return_value, result.return_value);
                printf("Session: %s\n", result.session_dir);
            }
        } else {
            fprintf(stderr, "Error [%s]: %s\n", result.stage, result.error);
        }
    }

    int rc = result.ok ? 0 : 1;
    c2asm_result_free(&result);
    return rc;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        config_print_usage(argv[0]);
        return 1;
    }

    Config cfg;
    if (!config_parse(argc, argv, &cfg)) {
        return 1;
    }

    if (cfg.profile_only) {
        return run_profile(&cfg);
    }

    if (cfg.scan_only) {
        return run_scan(&cfg);
    }

    if (cfg.c2asm_mode) {
        return run_c2asm(&cfg);
    }

    if (cfg.request_text != NULL || cfg.request_file != NULL) {
        return run_request(&cfg);
    }

    config_print_usage(argv[0]);
    return 1;
}

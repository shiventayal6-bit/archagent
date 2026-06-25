// ArchAgent IDE - C-to-A64 Playground: pipeline implementation
// Orchestrates lex -> parse -> codegen -> assemble -> emulate.

#include "c2asm_pipeline.h"

#include "c2asm_lexer.h"
#include "c2asm_parser.h"
#include "c2asm_codegen.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define C2ASM_MAX_SOURCE_BYTES (256 * 1024)

// ---- small helpers ----

static void set_fail(C2AsmResult *out, const char *stage, const char *fmt, ...) {
    out->ok = false;
    snprintf(out->stage, sizeof(out->stage), "%s", stage);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(out->error, sizeof(out->error), fmt, ap);
    va_end(ap);
}

static bool ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return S_ISDIR(st.st_mode);
    return mkdir(path, 0755) == 0;
}

// create every directory along a path (like mkdir -p)
static bool ensure_dir_recursive(const char *path) {
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (len == 0) return false;
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (!ensure_dir(tmp)) return false;
            *p = '/';
        }
    }
    return ensure_dir(tmp);
}

static bool write_text_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    if (content && *content) {
        fputs(content, f);
    }
    fclose(f);
    return true;
}

static char *read_text_file(const char *path, size_t max_bytes) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    char *buf = malloc(max_bytes + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t n = fread(buf, 1, max_bytes, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

static bool file_executable(const char *path) {
    return path && access(path, X_OK) == 0;
}

// join dir + "/" + name into a fixed buffer with explicit bounds checking,
// so no truncation can occur for the short file names this module uses.
static void join_session_path(char *out, size_t out_size,
                              const char *dir, const char *name) {
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    if (dlen + 1 + nlen + 1 > out_size) {
        // pathological: leave a safe truncated copy of the directory
        size_t copy = out_size > 0 ? out_size - 1 : 0;
        if (copy > dlen) copy = dlen;
        memcpy(out, dir, copy);
        out[copy] = '\0';
        return;
    }
    memcpy(out, dir, dlen);
    out[dlen] = '/';
    memcpy(out + dlen + 1, name, nlen);
    out[dlen + 1 + nlen] = '\0';
}

// build "YYYYMMDD_HHMMSS_<pid>" exactly as sandbox.c does
static void make_session_id(char *out, size_t out_size) {
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm_info);
    snprintf(out, out_size, "%s_%ld", timestamp, (long) getpid());
}

// locate the assembler/emulator binary
static bool find_tool(const C2AsmOptions *opts, const char *explicit_path,
                      const char *tool_name, char *out, size_t out_size) {
    if (explicit_path && *explicit_path) {
        snprintf(out, out_size, "%s", explicit_path);
        return file_executable(out);
    }
    if (opts->repo_root && *opts->repo_root) {
        snprintf(out, out_size, "%s/src/%s", opts->repo_root, tool_name);
        if (file_executable(out)) return true;
    }
    const char *candidates[] = {
        "../../src/%s",
        "../src/%s",
        "src/%s",
        "../../../src/%s",
    };
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        snprintf(out, out_size, candidates[i], tool_name);
        if (file_executable(out)) return true;
    }
    return false;
}

// run a two-argument tool (tool in0 out0). captures nothing; returns exit code
// or -1 on spawn failure, -2 on timeout.
static int run_tool(const char *tool, const char *arg1, const char *arg2,
                    int timeout_seconds) {
    if (timeout_seconds <= 0) timeout_seconds = 10;

    pid_t pid = fork();
    if (pid < 0) return -1;

    if (pid == 0) {
        // child: silence the tools' verbose stdout/stderr
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            (void) dup2(devnull, STDOUT_FILENO);
            (void) dup2(devnull, STDERR_FILENO);
            if (devnull > STDERR_FILENO) close(devnull);
        }
        char *const argv[] = {
            (char *) tool, (char *) arg1, (char *) arg2, NULL
        };
        execv(tool, argv);
        _exit(127);
    }

    int status = 0;
    time_t start = time(NULL);
    for (;;) {
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) break;
        if (w < 0) return -1;
        if (time(NULL) - start > timeout_seconds) {
            kill(pid, SIGKILL);
            (void) waitpid(pid, &status, 0);
            return -2;
        }
        struct timespec ts = {0, 10 * 1000 * 1000}; // 10ms
        nanosleep(&ts, NULL);
    }

    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

// parse "X00 = <16 hex digits>" out of the emulator output text
static bool parse_x00(const char *text, uint64_t *out_val) {
    if (!text) return false;
    const char *p = text;
    while (p && *p) {
        // find start of a line
        if ((p == text || p[-1] == '\n')) {
            if (strncmp(p, "X00 = ", 6) == 0) {
                uint64_t v = 0;
                if (sscanf(p + 6, "%016" SCNx64, &v) == 1) {
                    *out_val = v;
                    return true;
                }
            }
        }
        const char *nl = strchr(p, '\n');
        if (!nl) break;
        p = nl + 1;
    }
    return false;
}

// ---- pipeline ----

bool c2asm_pipeline_run(const C2AsmOptions *opts, C2AsmResult *out) {
    memset(out, 0, sizeof(*out));
    out->ok = true;
    out->return_value_available = false;

    if (!opts) {
        set_fail(out, "internal", "no options provided");
        return false;
    }

    int timeout = opts->timeout_seconds > 0 ? opts->timeout_seconds : 10;

    // 1. session directory: <base>/sessions/<id>/c2asm/
    const char *base = opts->session_base_dir ? opts->session_base_dir : ".archagent";
    make_session_id(out->session_id, sizeof(out->session_id));
    snprintf(out->session_dir, sizeof(out->session_dir),
             "%s/sessions/%s/c2asm", base, out->session_id);
    if (!ensure_dir_recursive(out->session_dir)) {
        set_fail(out, "internal", "failed to create session directory: %s",
                 out->session_dir);
        return false;
    }

    // 2. read source
    char *source = NULL;
    if (opts->source_text) {
        source = strdup(opts->source_text);
        if (!source) { set_fail(out, "internal", "out of memory"); return false; }
    } else if (opts->source_file) {
        source = read_text_file(opts->source_file, C2ASM_MAX_SOURCE_BYTES);
        if (!source) {
            set_fail(out, "internal", "could not read source file: %s",
                     opts->source_file);
            return false;
        }
    } else {
        set_fail(out, "internal", "no source provided (use source_text or source_file)");
        return false;
    }

    // 3. save input.cmini
    join_session_path(out->input_path, sizeof(out->input_path),
                      out->session_dir, "input.cmini");
    write_text_file(out->input_path, source);

    // 4. lex + 5. parse
    Lexer lexer;
    lexer_init(&lexer, source);

    Parser parser;
    if (!parser_init(&parser, &lexer, NULL, 0)) {
        set_fail(out, "lex", "%s", parser.error[0] ? parser.error : lexer.error);
        free(source);
        return false;
    }

    AstNode *program = parser_parse(&parser);
    if (!program) {
        set_fail(out, "parse", "%s",
                 parser.error[0] ? parser.error : "parse failed");
        free(source);
        return false;
    }

    // 6. codegen
    Codegen cg;
    if (!codegen_init(&cg)) {
        set_fail(out, "internal", "codegen init failed");
        ast_free(program);
        free(source);
        return false;
    }
    if (!codegen_generate(&cg, program)) {
        set_fail(out, "codegen", "%s",
                 cg.error[0] ? cg.error : "code generation failed");
        codegen_free(&cg);
        ast_free(program);
        free(source);
        return false;
    }

    out->generated_assembly = codegen_get_assembly(&cg);
    codegen_free(&cg);
    ast_free(program);
    free(source);

    if (!out->generated_assembly) {
        set_fail(out, "internal", "out of memory building assembly");
        return false;
    }

    // 7. save generated.s
    join_session_path(out->assembly_path, sizeof(out->assembly_path),
                      out->session_dir, "generated.s");
    write_text_file(out->assembly_path, out->generated_assembly);

    // 8. emit-asm-only short circuit
    if (opts->emit_asm_only) {
        out->ok = true;
        snprintf(out->stage, sizeof(out->stage), "codegen");
        return true;
    }

    // 9. find assembler
    char assemble_bin[4096];
    if (!find_tool(opts, opts->assemble_bin, "assemble",
                   assemble_bin, sizeof(assemble_bin))) {
        set_fail(out, "assemble",
                 "Could not find assembler. Use --assemble-bin <path>.");
        return false;
    }

    // 10. run assembler: assemble generated.s program.bin
    join_session_path(out->binary_path, sizeof(out->binary_path),
                      out->session_dir, "program.bin");
    int arc = run_tool(assemble_bin, out->assembly_path, out->binary_path, timeout);
    if (arc == -2) {
        set_fail(out, "assemble", "assembler timed out after %d seconds", timeout);
        return false;
    }
    if (arc != 0) {
        set_fail(out, "assemble",
                 "assembler exited with code %d (binary: %s)", arc, assemble_bin);
        return false;
    }

    // 11. find emulator
    char emulate_bin[4096];
    if (!find_tool(opts, opts->emulate_bin, "emulate",
                   emulate_bin, sizeof(emulate_bin))) {
        set_fail(out, "emulate",
                 "Could not find emulator. Use --emulate-bin <path>.");
        return false;
    }

    // 12. run emulator: emulate program.bin emulator.out
    join_session_path(out->emulator_output_path, sizeof(out->emulator_output_path),
                      out->session_dir, "emulator.out");
    int erc = run_tool(emulate_bin, out->binary_path, out->emulator_output_path, timeout);
    if (erc == -2) {
        set_fail(out, "emulate", "emulator timed out after %d seconds", timeout);
        return false;
    }
    if (erc != 0) {
        set_fail(out, "emulate",
                 "emulator exited with code %d (binary: %s)", erc, emulate_bin);
        return false;
    }

    // 13. parse X00
    out->emulator_output = read_text_file(out->emulator_output_path,
                                          C2ASM_MAX_SOURCE_BYTES);
    if (out->emulator_output) {
        uint64_t val = 0;
        if (parse_x00(out->emulator_output, &val)) {
            out->return_value = val;
            out->return_value_available = true;
        }
    }
    if (!out->return_value_available) {
        set_fail(out, "result_parse",
                 "could not find X00 in emulator output");
        return false;
    }

    // 14. save result.json
    join_session_path(out->result_json_path, sizeof(out->result_json_path),
                      out->session_dir, "result.json");
    {
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "{\n"
                 "  \"ok\": true,\n"
                 "  \"session_id\": \"%s\",\n"
                 "  \"return_value\": %" PRIu64 ",\n"
                 "  \"return_value_hex\": \"0x%016" PRIX64 "\"\n"
                 "}\n",
                 out->session_id, out->return_value, out->return_value);
        write_text_file(out->result_json_path, buf);
    }

    out->ok = true;
    snprintf(out->stage, sizeof(out->stage), "emulate");
    return true;
}

void c2asm_result_free(C2AsmResult *result) {
    if (!result) return;
    free(result->generated_assembly);
    free(result->emulator_output);
    result->generated_assembly = NULL;
    result->emulator_output = NULL;
}

// append a JSON-escaped string (with surrounding quotes) to stdout
static void print_json_string(const char *s) {
    putchar('"');
    if (s) {
        for (const char *p = s; *p; p++) {
            unsigned char c = (unsigned char) *p;
            switch (c) {
                case '"':  fputs("\\\"", stdout); break;
                case '\\': fputs("\\\\", stdout); break;
                case '\n': fputs("\\n", stdout);  break;
                case '\r': fputs("\\r", stdout);  break;
                case '\t': fputs("\\t", stdout);  break;
                case '\b': fputs("\\b", stdout);  break;
                case '\f': fputs("\\f", stdout);  break;
                default:
                    if (c < 0x20) printf("\\u%04x", c);
                    else          putchar((int) c);
            }
        }
    }
    putchar('"');
}

void c2asm_result_print_json(const C2AsmResult *result, const C2AsmOptions *opts) {
    (void) opts;
    printf("{\n");
    printf("  \"ok\": %s,\n", result->ok ? "true" : "false");
    printf("  \"stage\": ");      print_json_string(result->stage);      printf(",\n");
    printf("  \"error\": ");      print_json_string(result->error);      printf(",\n");
    printf("  \"session_id\": "); print_json_string(result->session_id); printf(",\n");
    printf("  \"session_dir\": ");print_json_string(result->session_dir);printf(",\n");
    printf("  \"input_path\": "); print_json_string(result->input_path); printf(",\n");
    printf("  \"assembly_path\": "); print_json_string(result->assembly_path); printf(",\n");
    printf("  \"binary_path\": "); print_json_string(result->binary_path); printf(",\n");
    printf("  \"emulator_output_path\": "); print_json_string(result->emulator_output_path); printf(",\n");
    printf("  \"result_json_path\": "); print_json_string(result->result_json_path); printf(",\n");
    printf("  \"generated_assembly\": "); print_json_string(result->generated_assembly); printf(",\n");
    printf("  \"emulator_output\": "); print_json_string(result->emulator_output); printf(",\n");
    printf("  \"return_value_available\": %s,\n",
           result->return_value_available ? "true" : "false");
    if (result->return_value_available) {
        printf("  \"return_value\": %" PRIu64 ",\n", result->return_value);
        printf("  \"return_value_hex\": \"0x%016" PRIX64 "\"\n", result->return_value);
    } else {
        printf("  \"return_value\": null,\n");
        printf("  \"return_value_hex\": null\n");
    }
    printf("}\n");
}

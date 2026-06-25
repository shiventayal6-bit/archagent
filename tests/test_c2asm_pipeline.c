// ArchAgent IDE - C-to-A64 Playground: pipeline integration tests
// Runs the full lex -> parse -> codegen -> assemble -> emulate path.
// If the assembler/emulator binaries cannot be found, the test SKIPs and
// returns 0 (not a failure) so the suite still passes on machines without them.

#include "c2asm_pipeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

// candidate directories that may contain the assemble/emulate binaries
static const char *CANDIDATE_DIRS[] = {
    "../../src",
    "../src",
    "../../../src",
    "src",
};

// fill assemble_out/emulate_out with the first directory containing both tools.
// returns true if found.
static bool locate_tools(char *assemble_out, char *emulate_out, size_t n) {
    for (size_t i = 0; i < sizeof(CANDIDATE_DIRS) / sizeof(CANDIDATE_DIRS[0]); i++) {
        char a[1024];
        char e[1024];
        snprintf(a, sizeof(a), "%s/assemble", CANDIDATE_DIRS[i]);
        snprintf(e, sizeof(e), "%s/emulate",  CANDIDATE_DIRS[i]);
        if (access(a, X_OK) == 0 && access(e, X_OK) == 0) {
            snprintf(assemble_out, n, "%s", a);
            snprintf(emulate_out, n, "%s", e);
            return true;
        }
    }
    return false;
}

static void run_case(const char *assemble_bin, const char *emulate_bin,
                     const char *src, uint64_t expected) {
    C2AsmOptions opts = {0};
    opts.source_text     = src;
    opts.assemble_bin    = assemble_bin;
    opts.emulate_bin     = emulate_bin;
    opts.session_base_dir = ".archagent_test";
    opts.timeout_seconds = 15;
    opts.keep_artifacts  = true;

    C2AsmResult result;
    bool ok = c2asm_pipeline_run(&opts, &result);

    if (!ok || !result.ok) {
        printf("FAIL: pipeline failed for [%s] stage=%s error=%s\n",
               src, result.stage, result.error);
        failures++;
    } else if (!result.return_value_available) {
        printf("FAIL: no return value for [%s]\n", src);
        failures++;
    } else if (result.return_value != expected) {
        printf("FAIL: [%s] expected %llu got %llu\n",
               src, (unsigned long long) expected,
               (unsigned long long) result.return_value);
        failures++;
    } else {
        printf("PASS: [%s] -> %llu\n", src,
               (unsigned long long) result.return_value);
    }

    c2asm_result_free(&result);
}

int main(void) {
    char assemble_bin[1024];
    char emulate_bin[1024];

    if (!locate_tools(assemble_bin, emulate_bin, sizeof(assemble_bin))) {
        printf("test_c2asm_pipeline: SKIP (assemble/emulate binaries not found)\n");
        return 0;
    }

    printf("test_c2asm_pipeline: using %s and %s\n", assemble_bin, emulate_bin);

    run_case(assemble_bin, emulate_bin, "int a = 5; return a;", 5);
    run_case(assemble_bin, emulate_bin, "int a = 6; int b = 7; return a * b;", 42);
    run_case(assemble_bin, emulate_bin,
             "int n = 5; int r = 1; while (n > 1) { r = r * n; n = n - 1; } return r;", 120);
    run_case(assemble_bin, emulate_bin,
             "int a = 10; int b = 20; if (a < b) { return b; } else { return a; }", 20);
    run_case(assemble_bin, emulate_bin,
             "int n = 10; int sum = 0; while (n > 0) { sum = sum + n; n = n - 1; } return sum;", 55);

    if (failures == 0) {
        printf("test_c2asm_pipeline: all tests passed\n");
        return 0;
    }
    printf("test_c2asm_pipeline: %d failure(s)\n", failures);
    return 1;
}

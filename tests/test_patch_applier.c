// Created by sm4925 on 2026/6/14

#include "diff_parser.h"
#include "patch_validator.h"
#include "patch_applier.h"
#include "test_helpers.h"

int main(void) {
    char dir[256];
    TEST_ASSERT(test_make_temp_dir(dir, sizeof(dir), "archagent_apply"));

    char path[512];
    snprintf(path, sizeof(path), "%s/file.c", dir);
    TEST_ASSERT(test_write_file(path, "one\ntwo\nthree\n"));

    const char *patch =
        "--- a/file.c\n"
        "+++ b/file.c\n"
        "@@ -1,3 +1,4 @@\n"
        " one\n"
        "-two\n"
        "+TWO\n"
        "+inserted\n"
        " three\n";

    ParsedDiff diff;
    TEST_ASSERT(diff_parse(patch, &diff));
    ValidationReport validation;
    TEST_ASSERT(patch_validate(&diff, dir, &validation));
    PatchApplyReport report;
    TEST_ASSERT(patch_apply_to_sandbox(&diff, dir, &report));
    TEST_ASSERT(test_file_contains(path, "TWO"));
    TEST_ASSERT(test_file_contains(path, "inserted"));
    parsed_diff_free(&diff);

    printf("test_patch_applier: PASS\n");
    return 0;
}
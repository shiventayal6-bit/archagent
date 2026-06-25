// Created by sm4925 on 2026/6/14

#include "diff_parser.h"
#include "patch_validator.h"
#include "test_helpers.h"

static int expect_validation(const char *patch, int expected_ok) {
    ParsedDiff diff;
    TEST_ASSERT(diff_parse(patch, &diff));
    ValidationReport report;
    bool ok = patch_validate(&diff, ".", &report);
    parsed_diff_free(&diff);
    TEST_ASSERT(ok == expected_ok);
    return 0;
}

int main(void) {
    const char *safe =
        "--- a/main.c\n+++ b/main.c\n@@ -1,1 +1,1 @@\n-old\n+new\n";
    TEST_ASSERT(expect_validation(safe, 1) == 0);

    const char *absolute =
        "--- a//etc/passwd\n+++ b//etc/passwd\n@@ -1,1 +1,1 @@\n-old\n+new\n";
    TEST_ASSERT(expect_validation(absolute, 0) == 0);

    const char *parent =
        "--- a/../outside.c\n+++ b/../outside.c\n@@ -1,1 +1,1 @@\n-old\n+new\n";
    TEST_ASSERT(expect_validation(parent, 0) == 0);

    const char *git =
        "--- a/.git/config\n+++ b/.git/config\n@@ -1,1 +1,1 @@\n-old\n+new\n";
    TEST_ASSERT(expect_validation(git, 0) == 0);

    printf("test_patch_validator: PASS\n");
    return 0;
}
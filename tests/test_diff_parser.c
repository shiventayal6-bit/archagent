// Created by sm4925 on 2026/6/14

#include "diff_parser.h"
#include "test_helpers.h"

int main(void) {
    const char *patch =
        "--- a/main.c\n"
        "+++ b/main.c\n"
        "@@ -1,2 +1,3 @@\n"
        " line1\n"
        "-old\n"
        "+new\n"
        "+extra\n";
    ParsedDiff diff;
    TEST_ASSERT(diff_parse(patch, &diff));
    TEST_ASSERT(diff.file_count == 1);
    TEST_ASSERT(strcmp(diff.files[0].old_path, "main.c") == 0);
    TEST_ASSERT(strcmp(diff.files[0].new_path, "main.c") == 0);
    TEST_ASSERT(diff.files[0].hunk_count == 1);
    TEST_ASSERT(diff.files[0].hunks[0].line_count == 4);
    parsed_diff_free(&diff);

    TEST_ASSERT(!diff_parse("not a diff", &diff));
    printf("test_diff_parser: PASS\n");
    return 0;
}
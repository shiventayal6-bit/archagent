// Created by sm4925 on 2026/6/14

#include "response_parser.h"
#include "test_helpers.h"

int main(void) {
    const char *valid =
        "PLAN:\n- Do it\n\n"
        "PATCH:\n```diff\n--- a/a.c\n+++ b/a.c\n@@ -1,1 +1,1 @@\n-old\n+new\n```\n\n"
        "TESTS:\n- make test\n";
    ParsedResponse parsed;
    TEST_ASSERT(response_parse(valid, &parsed));
    TEST_ASSERT_STR_CONTAINS(parsed.plan_text, "Do it");
    TEST_ASSERT_STR_CONTAINS(parsed.patch_text, "--- a/a.c");
    TEST_ASSERT_STR_CONTAINS(parsed.tests_text, "make test");
    parsed_response_free(&parsed);

    const char *invalid = "PLAN:\n- Missing patch\nTESTS:\n- none\n";
    TEST_ASSERT(!response_parse(invalid, &parsed));

    printf("test_response_parser: PASS\n");
    return 0;
}
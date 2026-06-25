// Created by sm4925 on 2026/6/14

#include "project_scanner.h"
#include "context_packer.h"
#include "test_helpers.h"

int main(void) {
    char dir[256];
    TEST_ASSERT(test_make_temp_dir(dir, sizeof(dir), "archagent_context"));

    char path[512];
    snprintf(path, sizeof(path), "%s/calculator.c", dir);
    TEST_ASSERT(test_write_file(path, "double calculate(void){return 1;}\n"));
    snprintf(path, sizeof(path), "%s/calculator.h", dir);
    TEST_ASSERT(test_write_file(path, "double calculate(void);\n"));
    snprintf(path, sizeof(path), "%s/Makefile", dir);
    TEST_ASSERT(test_write_file(path, "all:\n\tgcc calculator.c\n"));

    ProjectIndex index;
    TEST_ASSERT(project_scan(dir, &index));
    ContextPack pack;
    TEST_ASSERT(context_pack_build(dir, &index, "change calculator.c", 30000, &pack));
    TEST_ASSERT(pack.text != NULL);
    TEST_ASSERT_STR_CONTAINS(pack.text, "PROJECT CONTEXT");
    TEST_ASSERT_STR_CONTAINS(pack.text, "calculator.c");
    TEST_ASSERT(pack.files_included >= 1);
    context_pack_free(&pack);
    project_index_free(&index);

    printf("test_context_pack: PASS\n");
    return 0;
}
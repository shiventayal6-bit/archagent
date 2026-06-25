// Created by sm4925 on 2026/6/14

#include "project_scanner.h"
#include "test_helpers.h"

int main(void) {
    char dir[256];
    TEST_ASSERT(test_make_temp_dir(dir, sizeof(dir), "archagent_scan"));

    char path[512];
    snprintf(path, sizeof(path), "%s/main.c", dir);
    TEST_ASSERT(test_write_file(path, "int main(void){return 0;}\n"));
    snprintf(path, sizeof(path), "%s/main.h", dir);
    TEST_ASSERT(test_write_file(path, "#pragma once\n"));
    snprintf(path, sizeof(path), "%s/Makefile", dir);
    TEST_ASSERT(test_write_file(path, "all:\n\ttrue\n"));
    snprintf(path, sizeof(path), "%s/program.bin", dir);
    TEST_ASSERT(test_write_file(path, "binary-ish\n"));

    ProjectIndex index;
    TEST_ASSERT(project_scan(dir, &index));
    int saw_c = 0, saw_h = 0, saw_make = 0, saw_bin = 0;
    for (size_t i = 0; i < index.count; i++) {
        if (strcmp(index.files[i].relative_path, "main.c") == 0) saw_c = 1;
        if (strcmp(index.files[i].relative_path, "main.h") == 0) saw_h = 1;
        if (strcmp(index.files[i].relative_path, "Makefile") == 0) saw_make = 1;
        if (strcmp(index.files[i].relative_path, "program.bin") == 0) saw_bin = 1;
    }
    TEST_ASSERT(saw_c && saw_h && saw_make);
    TEST_ASSERT(!saw_bin);
    project_index_free(&index);

    printf("test_project_scan: PASS\n");
    return 0;
}
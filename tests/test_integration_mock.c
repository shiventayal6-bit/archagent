// Created by sm4925 on 2026/6/14

#include "test_helpers.h"

int main(void) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "%s --project demo_projects/wordcount --request 'Fix word counting for multiple spaces and newlines' --backend mock --yes --json > /tmp/archagent_integration_mock.out",
             "./bin/archagent");
    int status = system(cmd);
    TEST_ASSERT(status == 0);
    TEST_ASSERT(test_file_contains("/tmp/archagent_integration_mock.out", "\"patch_validated\": true"));
    TEST_ASSERT(test_file_contains("/tmp/archagent_integration_mock.out", "\"passed\": true"));
    printf("test_integration_mock: PASS\n");
    return 0;
}
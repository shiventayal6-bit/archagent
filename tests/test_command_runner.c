// Created by sm4925 on 2026/6/14

#include "command_runner.h"
#include "test_helpers.h"

#include <sys/stat.h>

int main(void) {
    char dir[256];
    TEST_ASSERT(test_make_temp_dir(dir, sizeof(dir), "archagent_cmd"));

    char test_path[512];
    snprintf(test_path, sizeof(test_path), "%s/test_ok", dir);
    TEST_ASSERT(test_write_file(test_path,
        "#!/bin/sh\n"
        "echo hello stdout\n"
        "echo hello stderr 1>&2\n"
        "exit 0\n"));
    chmod(test_path, 0755);

    CommandResult result;
    TEST_ASSERT(command_run_checked(dir, "./test_ok", 2, &result));
    TEST_ASSERT(result.exit_code == 0);
    TEST_ASSERT(!result.timed_out);
    TEST_ASSERT_STR_CONTAINS(result.stdout_text, "hello stdout");
    TEST_ASSERT_STR_CONTAINS(result.stderr_text, "hello stderr");
    command_result_free(&result);

    TEST_ASSERT(command_run_checked(dir, "rm -rf /", 2, &result));
    TEST_ASSERT(result.exit_code == 126);
    TEST_ASSERT_STR_CONTAINS(result.stderr_text, "Command rejected");
    command_result_free(&result);

    char sleep_path[512];
    snprintf(sleep_path, sizeof(sleep_path), "%s/test_sleep", dir);
    TEST_ASSERT(test_write_file(sleep_path,
        "#!/bin/sh\n"
        "sleep 3\n"
        "echo should-not-print\n"));
    chmod(sleep_path, 0755);

    TEST_ASSERT(command_run_checked(dir, "./test_sleep", 1, &result));
    TEST_ASSERT(result.timed_out);
    TEST_ASSERT(result.exit_code == -1);
    TEST_ASSERT_STR_CONTAINS(result.stderr_text, "timed out");
    command_result_free(&result);

    printf("test_command_runner: PASS\n");
    return 0;
}
// ArchAgent — Command runner
// Executes shell commands inside a sandbox with allowlist validation,
// timeout enforcement, and separate stdout/stderr capture.

#ifndef ARCHAGENT_COMMAND_RUNNER_H
#define ARCHAGENT_COMMAND_RUNNER_H

#include <stdbool.h>

typedef struct {
    int   exit_code;
    bool  timed_out;
    char *stdout_text;
    char *stderr_text;
} CommandResult;

// run a command string (e.g. "make test") inside working_dir
// validates the command against an allow/deny list before running
// returns true if the command was allowed and executed (regardless of
// its exit code) - returns false only if the command itself was rejected
bool command_run_checked(
    const char    *working_dir,
    const char    *command,
    int            timeout_seconds,
    CommandResult *out
);

// free memory allocated by command_run_checked
void command_result_free(CommandResult *result);

#endif // ARCHAGENT_COMMAND_RUNNER_H

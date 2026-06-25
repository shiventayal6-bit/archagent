// Created by st3125 on 2026/6/14
// ArchAgent IDE - llama.cpp backend

#include "llama_backend.h"

#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_OUT_LEN 65536

// search $PATH for an executable file named `name`
static bool find_on_path(const char *name) {
    const char *path_env = getenv("PATH");
    if (!path_env) return false;

    char *path_copy = strdup(path_env);
    if (!path_copy) return false;

    bool found = false;
    char *saveptr;
    char *dir = strtok_r(path_copy, ":", &saveptr);
    while (dir != NULL) {
        char candidate[PATH_MAX];
        snprintf(candidate, sizeof(candidate), "%s/%s", dir, name);
        if (access(candidate, X_OK) == 0) {
            found = true;
            break;
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }

    free(path_copy);
    return found;
}

// read all available data from a pipe fd into a malloc'd buffer
static char *read_all_from_fd(int fd) {
    char *buf = malloc(MAX_OUT_LEN);
    if (!buf) return NULL;

    size_t total = 0;
    ssize_t n;
    while (total < MAX_OUT_LEN - 1) {
        n = read(fd, buf + total, MAX_OUT_LEN - 1 - total);
        if (n <= 0) break;
        total += (size_t) n;
    }
    buf[total] = '\0';
    return buf;
}

bool llama_backend_generate(const char *prompt, const char *model_path,
                            int timeout_seconds, ModelResponse *out) {
    if (!prompt || !out) return false;

    out->text      = NULL;
    out->exit_code = -1;
    out->timed_out = false;

    if (!find_on_path("llama-cli")) {
        fprintf(stderr,
                "llama-cli not found. Install llama.cpp to use this backend. "
                "See README.md for instructions.\n");
        out->exit_code = 127;
        return false;
    }

    if (!model_path || access(model_path, F_OK) != 0) {
        fprintf(stderr, "No model path provided. Use --model <path-to-gguf>\n");
        out->exit_code = 2;
        return false;
    }

    // --- run "llama-cli -m <model_path> -p <prompt>" via fork/execvp ---
    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) return false;

    pid_t pid = fork();
    if (pid < 0) return false;

    if (pid == 0) {
        // child process
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);

        execlp("llama-cli", "llama-cli", "-m", model_path, "-p", prompt, (char *) NULL);
        _exit(127); // execlp failed
    }

    // parent process
    close(out_pipe[1]);
    close(err_pipe[1]);

    // set an alarm so we don't hang forever on a stuck command
    alarm((unsigned int) timeout_seconds);

    out->text = read_all_from_fd(out_pipe[0]);
    char *stderr_text = read_all_from_fd(err_pipe[0]);

    close(out_pipe[0]);
    close(err_pipe[0]);

    int status;
    pid_t result = waitpid(pid, &status, 0);
    alarm(0); // cancel the alarm

    if (result < 0) {
        // child likely killed by our alarm via SIGALRM default action
        out->timed_out = true;
        out->exit_code = -1;
    } else if (WIFEXITED(status)) {
        out->exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        out->timed_out = true;
        out->exit_code = -1;
        kill(pid, SIGKILL);
    }

    free(stderr_text);

    return out->exit_code == 0 && !out->timed_out && out->text != NULL;
}

// Created by st3125 on 2026/6/11
// Modified by sm4925 on 2026/6/14
// ArchAgent IDE - Command runner

#include "command_runner.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_ARGS       64
#define MAX_OUT_LEN    65536
#define POLL_SLICE_MS  100

static const char *FORBIDDEN_TOKENS[] = {
    "sudo", "rm", "curl", "wget", "ssh", "scp", "chmod", "chown", "dd", "mkfs",
    "python", "python3", "bash", "sh", "powershell", "cmd"
};

static const char *FORBIDDEN_SUBSTRINGS[] = {
    ";", "|", "&", ">", "<", "`", "$(", "${", "\n", "\r"
};

static bool contains_forbidden_substring(const char *command) {
    size_t n = sizeof(FORBIDDEN_SUBSTRINGS) / sizeof(FORBIDDEN_SUBSTRINGS[0]);
    for (size_t i = 0; i < n; i++) {
        if (strstr(command, FORBIDDEN_SUBSTRINGS[i]) != NULL) return true;
    }
    return false;
}

static bool token_forbidden(const char *token) {
    size_t n = sizeof(FORBIDDEN_TOKENS) / sizeof(FORBIDDEN_TOKENS[0]);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(token, FORBIDDEN_TOKENS[i]) == 0) return true;
    }
    return false;
}

static bool first_word_allowed(const char *first_word) {
    return strcmp(first_word, "make")  == 0 ||
           strcmp(first_word, "gcc")   == 0 ||
           strcmp(first_word, "clang") == 0 ||
           strncmp(first_word, "./test_",  7) == 0 ||
           strncmp(first_word, "./bench_", 8) == 0;
}

static int tokenize_command(const char *command, char **argv, int max_args) {
    char *copy = strdup(command);
    if (!copy) return -1;

    int argc = 0;
    char *saveptr = NULL;
    char *tok = strtok_r(copy, " \t", &saveptr);
    while (tok != NULL && argc < max_args - 1) {
        argv[argc] = strdup(tok);
        if (!argv[argc]) {
            for (int i = 0; i < argc; i++) free(argv[i]);
            free(copy);
            return -1;
        }
        argc++;
        tok = strtok_r(NULL, " \t", &saveptr);
    }
    argv[argc] = NULL;
    free(copy);
    return argc;
}

static void free_argv(char **argv, int argc) {
    for (int i = 0; i < argc; i++) free(argv[i]);
}

void command_result_free(CommandResult *result) {
    if (!result) return;
    free(result->stdout_text);
    free(result->stderr_text);
    result->stdout_text = NULL;
    result->stderr_text = NULL;
}

static char *xstrdup_empty(void) {
    char *s = malloc(1);
    if (s) s[0] = '\0';
    return s;
}

static void set_rejected(CommandResult *out, const char *message) {
    out->exit_code = 126;
    out->timed_out = false;
    out->stdout_text = xstrdup_empty();
    out->stderr_text = strdup(message ? message : "Command rejected");
}

static bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} CaptureBuffer;

static bool cap_init(CaptureBuffer *b) {
    b->cap = 4096;
    b->len = 0;
    b->data = malloc(b->cap);
    if (!b->data) return false;
    b->data[0] = '\0';
    return true;
}

static void cap_free(CaptureBuffer *b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

static bool cap_append(CaptureBuffer *b, const char *src, size_t n) {
    if (n == 0 || b->len >= MAX_OUT_LEN - 1) return true;
    if (b->len + n > MAX_OUT_LEN - 1) n = MAX_OUT_LEN - 1 - b->len;
    if (b->len + n + 1 > b->cap) {
        size_t new_cap = b->cap;
        while (new_cap < b->len + n + 1) new_cap *= 2;
        if (new_cap > MAX_OUT_LEN) new_cap = MAX_OUT_LEN;
        char *new_data = realloc(b->data, new_cap);
        if (!new_data) return false;
        b->data = new_data;
        b->cap = new_cap;
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
    b->data[b->len] = '\0';
    return true;
}

static void drain_fd_once(int fd, CaptureBuffer *buffer, bool *open_flag) {
    char tmp[4096];
    while (*open_flag) {
        ssize_t n = read(fd, tmp, sizeof(tmp));
        if (n > 0) {
            (void)cap_append(buffer, tmp, (size_t)n);
        } else if (n == 0) {
            close(fd);
            *open_flag = false;
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
            close(fd);
            *open_flag = false;
            return;
        }
    }
}

static long long monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (long long)ts.tv_sec * 1000LL + (long long)ts.tv_nsec / 1000000LL;
}

static bool validate_tokens(char **argv, int argc, CommandResult *out) {
    if (argc <= 0) {
        set_rejected(out, "Command rejected: empty command");
        return false;
    }
    if (!first_word_allowed(argv[0])) {
        set_rejected(out, "Command rejected: program not in allow-list");
        return false;
    }
    for (int i = 0; i < argc; i++) {
        if (token_forbidden(argv[i])) {
            set_rejected(out, "Command rejected: contains forbidden program token");
            return false;
        }
    }
    return true;
}

bool command_run_checked(
    const char    *working_dir,
    const char    *command,
    int            timeout_seconds,
    CommandResult *out)
{
    if (!working_dir || !command || !out) return false;

    out->exit_code = -1;
    out->timed_out = false;
    out->stdout_text = NULL;
    out->stderr_text = NULL;

    if (timeout_seconds <= 0) timeout_seconds = 1;
    if (strlen(command) == 0) {
        set_rejected(out, "Command rejected: empty command");
        return true;
    }
    if (contains_forbidden_substring(command)) {
        set_rejected(out, "Command rejected: contains forbidden shell syntax");
        return true;
    }

    char *argv[MAX_ARGS];
    int argc = tokenize_command(command, argv, MAX_ARGS);
    if (argc < 0) return false;

    if (!validate_tokens(argv, argc, out)) {
        free_argv(argv, argc);
        return true;
    }

    int stdout_pipe[2];
    int stderr_pipe[2];
    if (pipe(stdout_pipe) != 0) {
        free_argv(argv, argc);
        return false;
    }
    if (pipe(stderr_pipe) != 0) {
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        free_argv(argv, argc);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        free_argv(argv, argc);
        return false;
    }

    if (pid == 0) {
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        (void)dup2(stdout_pipe[1], STDOUT_FILENO);
        (void)dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        if (chdir(working_dir) != 0) _exit(127);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    (void)set_nonblocking(stdout_pipe[0]);
    (void)set_nonblocking(stderr_pipe[0]);

    CaptureBuffer stdout_buf = {0}, stderr_buf = {0};
    if (!cap_init(&stdout_buf) || !cap_init(&stderr_buf)) {
        kill(pid, SIGKILL);
        (void)waitpid(pid, NULL, 0);
        if (stdout_buf.data) cap_free(&stdout_buf);
        if (stderr_buf.data) cap_free(&stderr_buf);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        free_argv(argv, argc);
        return false;
    }

    bool stdout_open = true;
    bool stderr_open = true;
    bool child_done = false;
    int status = 0;
    long long start = monotonic_ms();
    long long timeout_ms = (long long)timeout_seconds * 1000LL;

    while (stdout_open || stderr_open || !child_done) {
        struct pollfd fds[2];
        nfds_t nfds = 0;
        if (stdout_open) {
            fds[nfds].fd = stdout_pipe[0];
            fds[nfds].events = POLLIN | POLLHUP | POLLERR;
            nfds++;
        }
        if (stderr_open) {
            fds[nfds].fd = stderr_pipe[0];
            fds[nfds].events = POLLIN | POLLHUP | POLLERR;
            nfds++;
        }

        if (nfds > 0) (void)poll(fds, nfds, POLL_SLICE_MS);

        if (stdout_open) drain_fd_once(stdout_pipe[0], &stdout_buf, &stdout_open);
        if (stderr_open) drain_fd_once(stderr_pipe[0], &stderr_buf, &stderr_open);

        if (!child_done) {
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) child_done = true;
        }

        if (!child_done && monotonic_ms() - start > timeout_ms) {
            out->timed_out = true;
            kill(pid, SIGKILL);
            (void)waitpid(pid, &status, 0);
            child_done = true;
        }
    }

    if (out->timed_out) {
        out->exit_code = -1;
        (void)cap_append(&stderr_buf, "Command timed out\n", strlen("Command timed out\n"));
    } else if (WIFEXITED(status)) {
        out->exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        out->exit_code = 128 + WTERMSIG(status);
    } else {
        out->exit_code = -1;
    }

    out->stdout_text = stdout_buf.data;
    out->stderr_text = stderr_buf.data;
    free_argv(argv, argc);
    return true;
}
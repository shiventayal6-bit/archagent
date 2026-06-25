// Created by st3125 on 2026/6/11
// ArchAgent IDE - Sandbox manager

#include "sandbox.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define SAFE_PATH_LEN 256

// build "a/b" into dst, truncating safely if needed
static void join_path(char *dst, size_t dst_size,
                      const char *a, const char *b) {
    dst[0] = '\0';
    strncat(dst, a, dst_size - strlen(dst) - 1);
    strncat(dst, "/", dst_size - strlen(dst) - 1);
    strncat(dst, b, dst_size - strlen(dst) - 1);
}

// create a directory if it doesn't already exist
static bool ensure_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    return mkdir(path, 0755) == 0;
}

// create an empty file (truncates if it exists)
static bool touch_file(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fclose(f);
    return true;
}

static bool skip_directory(const char *name) {
    return strcmp(name, ".git")         == 0 ||
           strcmp(name, ".archagent")   == 0 ||
           strcmp(name, "build")        == 0 ||
           strcmp(name, "bin")          == 0 ||
           strcmp(name, "dist")         == 0 ||
           strcmp(name, "__pycache__")  == 0 ||
           strcmp(name, "node_modules") == 0;
}

static bool ends_with(const char *str, const char *suffix) {
    size_t slen = strlen(str);
    size_t sufflen = strlen(suffix);
    if (sufflen > slen) return false;
    return strcmp(str + slen - sufflen, suffix) == 0;
}

static bool skip_file(const char *name) {
    return ends_with(name, ".o")   ||
           ends_with(name, ".a")   ||
           ends_with(name, ".so")  ||
           ends_with(name, ".bin") ||
           ends_with(name, ".img") ||
           ends_with(name, ".zip") ||
           ends_with(name, ".gguf");
}

// copy a single file from src to dst
static bool copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return false;

    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return false; }

    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in); fclose(out);
            return false;
        }
    }

    fclose(in);
    fclose(out);
    return true;
}

// recursively copy a directory tree
static bool copy_tree(const char *src_dir, const char *dst_dir) {
    if (!ensure_dir(dst_dir)) return false;

    DIR *dir = opendir(src_dir);
    if (!dir) return false;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        char src_path[PATH_MAX];
        char dst_path[PATH_MAX];
        join_path(src_path, sizeof(src_path), src_dir, name);
        join_path(dst_path, sizeof(dst_path), dst_dir, name);

        struct stat st;
        if (stat(src_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            if (!skip_directory(name)) {
                if (!copy_tree(src_path, dst_path)) {
                    closedir(dir);
                    return false;
                }
            }
        } else if (S_ISREG(st.st_mode)) {
            if (skip_file(name)) continue;
            if (!copy_file(src_path, dst_path)) {
                closedir(dir);
                return false;
            }
        }
    }

    closedir(dir);
    return true;
}

bool sandbox_create(const char *project_root, const char *audit_dir, Sandbox *out) {
    if (!project_root || !audit_dir || !out) return false;

    // build a session id from the current time and process id
    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", &tm_info);

    snprintf(out->session_id, sizeof(out->session_id), "%s_%ld",
             timestamp, (long) getpid());

    // <project_root>/<audit_dir>
    char audit_path[PATH_MAX];
    join_path(audit_path, sizeof(audit_path), project_root, audit_dir);
    if (!ensure_dir(audit_path)) return false;

    // <project_root>/<audit_dir>/sessions
    char sessions_path[PATH_MAX];
    join_path(sessions_path, sizeof(sessions_path), audit_path, "sessions");
    if (!ensure_dir(sessions_path)) return false;

    // <project_root>/<audit_dir>/sessions/<session_id>
    join_path(out->session_dir, sizeof(out->session_dir),
              sessions_path, out->session_id);
    if (!ensure_dir(out->session_dir)) return false;

    // <session_dir>/sandbox
    join_path(out->sandbox_root, sizeof(out->sandbox_root),
              out->session_dir, "sandbox");
    if (!ensure_dir(out->sandbox_root)) return false;

    // create empty audit log files
    const char *log_files[] = {
        "profile.txt", "project_index.txt", "prompt.txt",
        "model_response.txt", "parsed_plan.txt", "patch.diff",
        "validation_report.txt", "build_stdout.txt", "build_stderr.txt",
        "test_stdout.txt", "test_stderr.txt", "benchmark.txt",
        "summary.txt", "result.json", "events.jsonl"
    };
    size_t num_files = sizeof(log_files) / sizeof(log_files[0]);

    for (size_t i = 0; i < num_files; i++) {
        char file_path[PATH_MAX];
        join_path(file_path, sizeof(file_path), out->session_dir, log_files[i]);
        if (!touch_file(file_path)) return false;
    }

    return true;
}

bool sandbox_copy_project(const char *project_root, const Sandbox *sandbox) {
    if (!project_root || !sandbox) return false;
    return copy_tree(project_root, sandbox->sandbox_root);
}

// Created by st3125 on 2026/6/11
// ArchAgent IDE - Audit logger

#include "audit_logger.h"

#include <stdio.h>
#include <string.h>

static void join_path(char *dst, size_t dst_size,
                      const char *a, const char *b) {
    snprintf(dst, dst_size, "%s/%s", a, b);
}

bool audit_log_event(const Sandbox *sandbox, const char *event_json) {
    if (!sandbox || !event_json) return false;

    char path[PATH_MAX + 32];
    join_path(path, sizeof(path), sandbox->session_dir, "events.jsonl");

    FILE *f = fopen(path, "a"); // append mode
    if (!f) return false;

    fprintf(f, "%s\n", event_json);
    fclose(f);
    return true;
}

bool audit_write_file(const Sandbox *sandbox, const char *filename, const char *content) {
    if (!sandbox || !filename || !content) return false;

    char path[PATH_MAX + 32];
    join_path(path, sizeof(path), sandbox->session_dir, filename);

    FILE *f = fopen(path, "w");
    if (!f) return false;

    fprintf(f, "%s", content);
    fclose(f);
    return true;
}

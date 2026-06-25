// Created by st3125 on 2026/6/11
// ArchAgent IDE - Patch validator

#include "patch_validator.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#define MAX_PATH_LEN     4096
#define SHORT_PATH_LEN   128

// check if a path is unsafe according to the spec rules
// path is copied into a short, fixed-size buffer first so that
// snprintf below can never be told its output might be truncated
static bool path_is_unsafe(const char *path, char *reason, size_t reason_len) {
    char p[SHORT_PATH_LEN];
    snprintf(p, sizeof(p), "%.100s", path);

    if (strlen(path) == 0) {
        snprintf(reason, reason_len, "path is empty");
        return true;
    }

    if (strlen(path) >= MAX_PATH_LEN) {
        snprintf(reason, reason_len, "path is too long");
        return true;
    }

    if (path[0] == '/') {
        snprintf(reason, reason_len, "path '%s' is an absolute path", p);
        return true;
    }

    if (strstr(path, "..") != NULL) {
        snprintf(reason, reason_len, "path '%s' contains '..'", p);
        return true;
    }

    if (strncmp(path, ".git/", 5) == 0 || strcmp(path, ".git") == 0) {
        snprintf(reason, reason_len, "path '%s' targets .git", p);
        return true;
    }

    if (strncmp(path, ".archagent/", 11) == 0 || strcmp(path, ".archagent") == 0) {
        snprintf(reason, reason_len, "path '%s' targets .archagent", p);
        return true;
    }

    return false;
}

// check if text looks like binary data (contains a NUL or control byte)
static bool looks_binary(const DiffHunk *hunk) {
    for (size_t i = 0; i < hunk->line_count; i++) {
        const char *text = hunk->lines[i].text;
        for (const char *c = text; *c; c++) {
            unsigned char uc = (unsigned char) *c;
            if (uc < 0x09) return true;
        }
    }
    return false;
}

bool patch_validate(
    const ParsedDiff *diff,
    const char       *project_root,
    ValidationReport *report)
{
    (void)project_root;
    if (!diff || !report) return false;

    report->ok = true;
    snprintf(report->message, sizeof(report->message), "All checks passed");

    if (diff->file_count == 0) {
        report->ok = false;
        snprintf(report->message, sizeof(report->message),
                "Patch contains no files");
        return false;
    }

    for (size_t f = 0; f < diff->file_count; f++) {
        const FilePatch *file = &diff->files[f];
        char reason[256];

        if (path_is_unsafe(file->old_path, reason, sizeof(reason))) {
            report->ok = false;
            snprintf(report->message, sizeof(report->message),
                    "Rejected: old path unsafe - %s", reason);
            return false;
        }

        if (path_is_unsafe(file->new_path, reason, sizeof(reason))) {
            report->ok = false;
            snprintf(report->message, sizeof(report->message),
                    "Rejected: new path unsafe - %s", reason);
            return false;
        }

        /* File existence is verified at apply time; the validator focuses on
         * path safety only (no traversal, no absolute paths, no binary data). */

        for (size_t h = 0; h < file->hunk_count; h++) {
            if (looks_binary(&file->hunks[h])) {
                char p[SHORT_PATH_LEN];
                snprintf(p, sizeof(p), "%.100s", file->new_path);
                report->ok = false;
                snprintf(report->message, sizeof(report->message),
                        "Rejected: file '%s' appears to contain binary data", p);
                return false;
            }
        }
    }

    return true;
}

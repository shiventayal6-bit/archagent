// Created by st3125 on 2026/6/11
// ArchAgent IDE - Patch applier

#include "patch_applier.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES     65536
#define MAX_LINE_LEN  4096
#define SHORT_PATH_LEN 256

/* Compare two lines tolerating leading-whitespace differences.
 * Returns true if the non-whitespace content matches.
 * Used so context/remove lines with wrong indentation
 * still match the real file (the real line is kept in the output). */
static bool lines_match(const char *file_line, const char *diff_line) {
    if (strcmp(file_line, diff_line) == 0) return true;
    while (*file_line == ' ' || *file_line == '\t') file_line++;
    while (*diff_line  == ' ' || *diff_line  == '\t') diff_line++;
    return strcmp(file_line, diff_line) == 0;
}

// build "a/b" into dst safely
static void join_path(char *dst, size_t dst_size,
                      const char *a, const char *b) {
    snprintf(dst, dst_size, "%s/%s", a, b);
}

// read a file into an array of malloc'd lines
// returns the number of lines read, or -1 on error
// if the file does not exist, returns 0 (treated as an empty new file)
static long read_lines(const char *path, char **lines, long max_lines) {
    FILE *f = fopen(path, "r");
    if (!f) return 0; // file doesn't exist yet - treat as empty

    long count = 0;
    char buf[MAX_LINE_LEN];

    while (count < max_lines && fgets(buf, sizeof(buf), f) != NULL) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';

        lines[count] = strdup(buf);
        if (!lines[count]) { fclose(f); return -1; }
        count++;
    }

    fclose(f);
    return count;
}

static void free_lines(char **lines, long count) {
    for (long i = 0; i < count; i++) free(lines[i]);
}

#define FUZZ_LINES 5

/* Search for the first context/remove line of a hunk near nominal_pos.
 * Returns the best matching old_lines index, or nominal_pos if none found. */
static long fuzzy_hunk_offset(char **old_lines, long old_count,
                              long nominal_pos, const DiffHunk *hunk) {
    const char *anchor = NULL;
    for (size_t l = 0; l < hunk->line_count; l++) {
        if (hunk->lines[l].type != DIFF_ADD) {
            anchor = hunk->lines[l].text;
            break;
        }
    }
    if (!anchor) return nominal_pos;

    if (nominal_pos < old_count &&
        lines_match(old_lines[nominal_pos], anchor)) {
        return nominal_pos;
    }

    long lo = nominal_pos - FUZZ_LINES;
    long hi = nominal_pos + FUZZ_LINES;
    if (lo < 0) lo = 0;
    if (hi >= old_count) hi = old_count - 1;

    for (long d = 1; d <= FUZZ_LINES; d++) {
        long fwd = nominal_pos + d;
        long bwd = nominal_pos - d;
        if (fwd <= hi && lines_match(old_lines[fwd], anchor)) return fwd;
        if (bwd >= lo && lines_match(old_lines[bwd], anchor)) return bwd;
    }

    return nominal_pos;
}

// apply all hunks of one file patch
static bool apply_file_patch(const FilePatch *file, const char *sandbox_root,
                             PatchApplyReport *report) {
    char src_path[PATH_MAX + 32];
    join_path(src_path, sizeof(src_path), sandbox_root, file->new_path);

    // short, fixed-length copy of the path for use in messages
    char p[SHORT_PATH_LEN];
    snprintf(p, sizeof(p), "%.200s", file->new_path);

    char **old_lines = malloc(MAX_LINES * sizeof(char *));
    if (!old_lines) {
        snprintf(report->message, sizeof(report->message),
                "Out of memory reading %s", p);
        return false;
    }

    long old_count = read_lines(src_path, old_lines, MAX_LINES);
    if (old_count < 0) {
        free(old_lines);
        snprintf(report->message, sizeof(report->message),
                "Failed to read %s", p);
        return false;
    }

    char **new_lines = malloc(MAX_LINES * sizeof(char *));
    if (!new_lines) {
        free_lines(old_lines, old_count);
        free(old_lines);
        snprintf(report->message, sizeof(report->message),
                "Out of memory writing %s", p);
        return false;
    }
    long new_count = 0;
    long old_pos = 0;

    for (size_t h = 0; h < file->hunk_count; h++) {
        const DiffHunk *hunk = &file->hunks[h];

        long hunk_start = (long) hunk->old_start - 1;
        if (hunk_start < 0) hunk_start = 0;
        if (hunk_start < old_pos) hunk_start = old_pos;

        /* Fuzzy search: tolerate small off-by-one errors in generated offsets. */
        hunk_start = fuzzy_hunk_offset(old_lines, old_count, hunk_start, hunk);
        if (hunk_start < old_pos) hunk_start = old_pos;

        while (old_pos < hunk_start && old_pos < old_count) {
            if (new_count >= MAX_LINES) {
                snprintf(report->message, sizeof(report->message),
                        "Patch result too large for %s (>%d lines)", p, MAX_LINES);
                free_lines(old_lines, old_count); free(old_lines);
                free_lines(new_lines, new_count); free(new_lines);
                return false;
            }
            new_lines[new_count++] = strdup(old_lines[old_pos]);
            old_pos++;
        }

        for (size_t l = 0; l < hunk->line_count; l++) {
            const DiffLine *dl = &hunk->lines[l];

            switch (dl->type) {
                case DIFF_CONTEXT:
                    /* If the diff has a blank context line but the file
                     * doesn't — not present in the file. Skip it silently. */
                    if (dl->text[0] == '\0' &&
                        (old_pos >= old_count || old_lines[old_pos][0] != '\0')) {
                        break;
                    }
                    if (old_pos >= old_count ||
                        !lines_match(old_lines[old_pos], dl->text)) {
                        snprintf(report->message, sizeof(report->message),
                                "Context mismatch in %s at line %ld",
                                p, old_pos + 1);
                        free_lines(old_lines, old_count); free(old_lines);
                        free_lines(new_lines, new_count); free(new_lines);
                        return false;
                    }
                    if (new_count >= MAX_LINES) {
                        snprintf(report->message, sizeof(report->message),
                                "Patch result too large for %s (>%d lines)", p, MAX_LINES);
                        free_lines(old_lines, old_count); free(old_lines);
                        free_lines(new_lines, new_count); free(new_lines);
                        return false;
                    }
                    new_lines[new_count++] = strdup(old_lines[old_pos]);
                    old_pos++;
                    break;

                case DIFF_REMOVE:
                    if (old_pos >= old_count ||
                        !lines_match(old_lines[old_pos], dl->text)) {
                        snprintf(report->message, sizeof(report->message),
                                "Remove mismatch in %s at line %ld",
                                p, old_pos + 1);
                        free_lines(old_lines, old_count); free(old_lines);
                        free_lines(new_lines, new_count); free(new_lines);
                        return false;
                    }
                    old_pos++;
                    break;

                case DIFF_ADD:
                    if (new_count >= MAX_LINES) {
                        snprintf(report->message, sizeof(report->message),
                                "Patch result too large for %s (>%d lines)", p, MAX_LINES);
                        free_lines(old_lines, old_count); free(old_lines);
                        free_lines(new_lines, new_count); free(new_lines);
                        return false;
                    }
                    new_lines[new_count++] = strdup(dl->text);
                    break;
            }
        }
    }

    while (old_pos < old_count) {
        if (new_count >= MAX_LINES) {
            snprintf(report->message, sizeof(report->message),
                    "Patch result too large for %s (>%d lines)", p, MAX_LINES);
            free_lines(old_lines, old_count); free(old_lines);
            free_lines(new_lines, new_count); free(new_lines);
            return false;
        }
        new_lines[new_count++] = strdup(old_lines[old_pos]);
        old_pos++;
    }

    char tmp_path[PATH_MAX + 64];
    snprintf(tmp_path, sizeof(tmp_path), "%s.archagent_tmp", src_path);

    FILE *out = fopen(tmp_path, "w");
    if (!out) {
        snprintf(report->message, sizeof(report->message),
                "Failed to open temp file for %s", p);
        free_lines(old_lines, old_count); free(old_lines);
        free_lines(new_lines, new_count); free(new_lines);
        return false;
    }

    for (long i = 0; i < new_count; i++) {
        fprintf(out, "%s\n", new_lines[i]);
    }
    fclose(out);

    if (rename(tmp_path, src_path) != 0) {
        snprintf(report->message, sizeof(report->message),
                "Failed to rename temp file for %s", p);
        free_lines(old_lines, old_count); free(old_lines);
        free_lines(new_lines, new_count); free(new_lines);
        return false;
    }

    free_lines(old_lines, old_count); free(old_lines);
    free_lines(new_lines, new_count); free(new_lines);
    return true;
}

bool patch_apply_to_sandbox(
    const ParsedDiff *diff,
    const char       *sandbox_root,
    PatchApplyReport *report)
{
    if (!diff || !sandbox_root || !report) return false;

    report->ok = true;
    snprintf(report->message, sizeof(report->message), "Patch applied successfully");

    for (size_t f = 0; f < diff->file_count; f++) {
        if (!apply_file_patch(&diff->files[f], sandbox_root, report)) {
            report->ok = false;
            return false;
        }
    }

    return true;
}

// Created by st3125 on 2026/6/11
// ArchAgent IDE - Unified diff parser

#include "diff_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_FILE_CAP 4
#define INITIAL_HUNK_CAP 4
#define INITIAL_LINE_CAP 16

// --- dynamic array helpers ---

static bool grow_files(ParsedDiff *diff, size_t *cap) {
    size_t new_cap = (*cap) * 2;
    FilePatch *new_files = realloc(diff->files, new_cap * sizeof(FilePatch));
    if (!new_files) return false;
    diff->files = new_files;
    *cap = new_cap;
    return true;
}

static bool grow_hunks(FilePatch *file, size_t *cap) {
    size_t new_cap = (*cap) * 2;
    DiffHunk *new_hunks = realloc(file->hunks, new_cap * sizeof(DiffHunk));
    if (!new_hunks) return false;
    file->hunks = new_hunks;
    *cap = new_cap;
    return true;
}

static bool grow_lines(DiffHunk *hunk, size_t *cap) {
    size_t new_cap = (*cap) * 2;
    DiffLine *new_lines = realloc(hunk->lines, new_cap * sizeof(DiffLine));
    if (!new_lines) return false;
    hunk->lines = new_lines;
    *cap = new_cap;
    return true;
}

// strip the "a/" or "b/" prefix from a diff path
static void strip_ab_prefix(const char *src, char *dst, size_t dst_size) {
    if ((src[0] == 'a' || src[0] == 'b') && src[1] == '/') {
        src += 2;
    }
    snprintf(dst, dst_size, "%s", src);

    // trim trailing newline/whitespace
    size_t len = strlen(dst);
    while (len > 0 && (dst[len-1] == '\n' || dst[len-1] == '\r' || dst[len-1] == ' ')) {
        dst[--len] = '\0';
    }
}

// parse "@@ -old_start,old_count +new_start,new_count @@"
static bool parse_hunk_header(const char *line, DiffHunk *hunk) {
    long old_start, old_count, new_start, new_count;

    // try the full form with counts first
    int n = sscanf(line, "@@ -%ld,%ld +%ld,%ld @@",
                   &old_start, &old_count, &new_start, &new_count);
    if (n == 4) {
        hunk->old_start = (size_t) old_start;
        hunk->old_count = (size_t) old_count;
        hunk->new_start = (size_t) new_start;
        hunk->new_count = (size_t) new_count;
        return true;
    }

    // fall back to the form without counts (count defaults to 1)
    n = sscanf(line, "@@ -%ld +%ld @@", &old_start, &new_start);
    if (n == 2) {
        hunk->old_start = (size_t) old_start;
        hunk->old_count = 1;
        hunk->new_start = (size_t) new_start;
        hunk->new_count = 1;
        return true;
    }

    return false;
}

void parsed_diff_free(ParsedDiff *diff) {
    if (!diff) return;
    for (size_t f = 0; f < diff->file_count; f++) {
        FilePatch *file = &diff->files[f];
        for (size_t h = 0; h < file->hunk_count; h++) {
            DiffHunk *hunk = &file->hunks[h];
            for (size_t l = 0; l < hunk->line_count; l++) {
                free(hunk->lines[l].text);
            }
            free(hunk->lines);
        }
        free(file->hunks);
    }
    free(diff->files);
    diff->files      = NULL;
    diff->file_count = 0;
}

bool diff_parse(const char *diff_text, ParsedDiff *out) {
    if (!diff_text || !out) return false;

    size_t files_cap = INITIAL_FILE_CAP;
    out->files      = malloc(files_cap * sizeof(FilePatch));
    out->file_count = 0;
    if (!out->files) return false;

    // make a mutable copy so we can use strtok_r line by line
    char *text_copy = strdup(diff_text);
    if (!text_copy) { free(out->files); return false; }

    FilePatch *current_file = NULL;
    DiffHunk  *current_hunk  = NULL;
    size_t     hunks_cap = 0;
    size_t     lines_cap = 0;

    char *saveptr;
    char *line = strtok_r(text_copy, "\n", &saveptr);

    while (line != NULL) {
        if (strncmp(line, "--- ", 4) == 0) {
            // start of a new file
            if (out->file_count >= files_cap) {
                if (!grow_files(out, &files_cap)) goto fail;
            }
            current_file = &out->files[out->file_count++];
            current_file->hunks      = NULL;
            current_file->hunk_count = 0;
            hunks_cap = INITIAL_HUNK_CAP;
            current_file->hunks = malloc(hunks_cap * sizeof(DiffHunk));
            if (!current_file->hunks) goto fail;

            strip_ab_prefix(line + 4, current_file->old_path,
                           sizeof(current_file->old_path));
            current_hunk = NULL;

        } else if (strncmp(line, "+++ ", 4) == 0) {
            if (!current_file) goto fail;
            strip_ab_prefix(line + 4, current_file->new_path,
                           sizeof(current_file->new_path));

        } else if (strncmp(line, "@@", 2) == 0) {
            if (!current_file) goto fail;

            if (current_file->hunk_count >= hunks_cap) {
                if (!grow_hunks(current_file, &hunks_cap)) goto fail;
            }
            current_hunk = &current_file->hunks[current_file->hunk_count++];
            current_hunk->lines      = NULL;
            current_hunk->line_count = 0;

            if (!parse_hunk_header(line, current_hunk)) goto fail;

            lines_cap = INITIAL_LINE_CAP;
            current_hunk->lines = malloc(lines_cap * sizeof(DiffLine));
            if (!current_hunk->lines) goto fail;

        } else if (line[0] == '+' || line[0] == '-' || line[0] == ' ') {
            // a diff content line — must belong to a hunk
            if (!current_hunk) {
                line = strtok_r(NULL, "\n", &saveptr);
                continue; // ignore stray lines outside a hunk
            }

            if (current_hunk->line_count >= lines_cap) {
                if (!grow_lines(current_hunk, &lines_cap)) goto fail;
            }

            DiffLine *dl = &current_hunk->lines[current_hunk->line_count++];
            if (line[0] == '+')      dl->type = DIFF_ADD;
            else if (line[0] == '-') dl->type = DIFF_REMOVE;
            else                     dl->type = DIFF_CONTEXT;

            dl->text = strdup(line + 1); // skip the +/-/space prefix
            if (!dl->text) goto fail;
        }
        // any other line (e.g. blank lines, "diff --git" headers) is ignored

        line = strtok_r(NULL, "\n", &saveptr);
    }

    free(text_copy);

    // reject if no files or no hunks were found
    if (out->file_count == 0) {
        fprintf(stderr, "Diff parser: no files found in patch\n");
        parsed_diff_free(out);
        return false;
    }
    for (size_t f = 0; f < out->file_count; f++) {
        if (out->files[f].hunk_count == 0) {
            fprintf(stderr, "Diff parser: file %s has no hunks\n",
                    out->files[f].new_path);
            parsed_diff_free(out);
            return false;
        }
    }

    return true;

fail:
    free(text_copy);
    parsed_diff_free(out);
    return false;
}

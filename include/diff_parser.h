// ArchAgent — Unified diff parser
// Parses unified diff text into a structured representation
// (ParsedDiff → FilePatch → DiffHunk → DiffLine) suitable for validation
// and fuzzy application.

#ifndef ARCHAGENT_DIFF_PARSER_H
#define ARCHAGENT_DIFF_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

typedef enum {
    DIFF_CONTEXT,
    DIFF_ADD,
    DIFF_REMOVE
} DiffLineType;

typedef struct {
    DiffLineType type;
    char *text;
} DiffLine;

typedef struct {
    size_t    old_start;
    size_t    old_count;
    size_t    new_start;
    size_t    new_count;
    DiffLine *lines;
    size_t    line_count;
} DiffHunk;

typedef struct {
    char      old_path[PATH_MAX];
    char      new_path[PATH_MAX];
    DiffHunk *hunks;
    size_t    hunk_count;
} FilePatch;

typedef struct {
    FilePatch *files;
    size_t     file_count;
} ParsedDiff;

// parse a unified diff text into structured data
// returns true on success, false on malformed input
bool diff_parse(const char *diff_text, ParsedDiff *out);

// free memory allocated by diff_parse
void parsed_diff_free(ParsedDiff *diff);

#endif // ARCHAGENT_DIFF_PARSER_H

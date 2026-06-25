// Created by st3125 on 2026/6/11
// ArchAgent IDE - Project scanner

#ifndef ARCHAGENT_PROJECT_SCANNER_H
#define ARCHAGENT_PROJECT_SCANNER_H

#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <linux/limits.h>

// information about one file in the project
typedef struct {
    char   relative_path[PATH_MAX];
    size_t size_bytes;
    bool   is_source;
    bool   is_header;
    bool   is_makefile;
    bool   is_test;
} ProjectFile;

// list of all files found in the project
typedef struct {
    ProjectFile *files;
    size_t       count;
    size_t       capacity;
} ProjectIndex;

// scan the project at root and fill out
// returns true on success, false on failure
bool project_scan(const char *root, ProjectIndex *out);

// free memory allocated by project_scan
void project_index_free(ProjectIndex *index);

#endif // ARCHAGENT_PROJECT_SCANNER_H

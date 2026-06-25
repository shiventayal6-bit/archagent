// Created by st3125 on 2026/6/11
// ArchAgent IDE - Project scanner

#include "project_scanner.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define INITIAL_CAPACITY 64
#define MAX_FILE_SIZE    (5 * 1024 * 1024)  // skip files over 5MB

// check if a filename ends with a given suffix
static bool ends_with(const char *str, const char *suffix) {
    size_t slen = strlen(str);
    size_t sufflen = strlen(suffix);
    if (sufflen > slen) return false;
    return strcmp(str + slen - sufflen, suffix) == 0;
}

// check if a directory name should be skipped entirely
static bool skip_directory(const char *name) {
    return strcmp(name, ".git")         == 0 ||
           strcmp(name, ".archagent")   == 0 ||
           strcmp(name, "build")        == 0 ||
           strcmp(name, "bin")          == 0 ||
           strcmp(name, "dist")         == 0 ||
           strcmp(name, "__pycache__")  == 0 ||
           strcmp(name, "node_modules") == 0;
}

// check if a file should be skipped
static bool skip_file(const char *name) {
    return ends_with(name, ".o")      ||
           ends_with(name, ".a")      ||
           ends_with(name, ".so")     ||
           ends_with(name, ".dylib")  ||
           ends_with(name, ".exe")    ||
           ends_with(name, ".bin")    ||
           ends_with(name, ".img")    ||
           ends_with(name, ".zip")    ||
           ends_with(name, ".gguf")   ||
           ends_with(name, ".pyc");
}

// check what kind of file this is
static bool is_source_file(const char *name) {
    return ends_with(name, ".c") ||
           ends_with(name, ".s") ||
           ends_with(name, ".S");
}

static bool is_header_file(const char *name) {
    return ends_with(name, ".h");
}

static bool is_makefile(const char *name) {
    return strcmp(name, "Makefile") == 0 ||
           strcmp(name, "makefile") == 0;
}

static bool is_test_file(const char *name) {
    return strncmp(name, "test_", 5) == 0 ||
           strncmp(name, "tests_", 6) == 0 ||
           ends_with(name, "_test.c");
}

// add one file to the index, growing the array if needed
static bool index_add(ProjectIndex *idx, ProjectFile *file) {
    if (idx->count >= idx->capacity) {
        size_t new_cap = idx->capacity * 2;
        ProjectFile *new_files = realloc(idx->files,
                                         new_cap * sizeof(ProjectFile));
        if (!new_files) return false;
        idx->files    = new_files;
        idx->capacity = new_cap;
    }
    idx->files[idx->count++] = *file;
    return true;
}

// recursively scan a directory
// rel_prefix is the path relative to the project root
static bool scan_dir(const char *abs_path, const char *rel_prefix,
                     ProjectIndex *idx) {
    DIR *dir = opendir(abs_path);
    if (!dir) return false;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;

        // skip . and ..
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        // build absolute path of this entry
        char abs_entry[PATH_MAX];
        snprintf(abs_entry, sizeof(abs_entry), "%s/%s", abs_path, name);

        // build relative path from project root
        char rel_entry[PATH_MAX];
        if (strlen(rel_prefix) == 0) {
            snprintf(rel_entry, sizeof(rel_entry), "%s", name);
        } else {
            snprintf(rel_entry, sizeof(rel_entry), "%s/%s", rel_prefix, name);
        }

        // get file info
        struct stat st;
        if (stat(abs_entry, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            // recurse into subdirectory unless it should be skipped
            if (!skip_directory(name)) {
                scan_dir(abs_entry, rel_entry, idx);
            }
        } else if (S_ISREG(st.st_mode)) {
            // skip unwanted file types
            if (skip_file(name)) continue;

            // skip files that are too large
            if ((size_t)st.st_size > MAX_FILE_SIZE) continue;

            // build the ProjectFile entry
            ProjectFile file;
            memset(&file, 0, sizeof(file));
            snprintf(file.relative_path, sizeof(file.relative_path), "%s", rel_entry);
            file.size_bytes  = (size_t) st.st_size;
            file.is_source   = is_source_file(name);
            file.is_header   = is_header_file(name);
            file.is_makefile = is_makefile(name);
            file.is_test     = is_test_file(name);

            if (!index_add(idx, &file)) {
                closedir(dir);
                return false;
            }
        }
    }

    closedir(dir);
    return true;
}

void project_index_free(ProjectIndex *index) {
    if (index && index->files) {
        free(index->files);
        index->files    = NULL;
        index->count    = 0;
        index->capacity = 0;
    }
}

bool project_scan(const char *root, ProjectIndex *out) {
    if (!root || !out) return false;

    out->files    = malloc(INITIAL_CAPACITY * sizeof(ProjectFile));
    if (!out->files) return false;
    out->count    = 0;
    out->capacity = INITIAL_CAPACITY;

    return scan_dir(root, "", out);
}

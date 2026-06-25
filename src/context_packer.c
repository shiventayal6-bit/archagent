// Created by st3125 on 2026/6/11
// ArchAgent IDE - Context packer

#include "context_packer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILE_READ 100000
#define ABS_PATH_MAX  16384

static void build_abs_path(char *buf, size_t bufsize,
                            const char *root, const char *rel) {
    buf[0] = '\0';
    strncat(buf, root, bufsize - 1);
    strncat(buf, "/",  bufsize - strlen(buf) - 1);
    strncat(buf, rel,  bufsize - strlen(buf) - 1);
}

static bool contains_keyword(const char *text, const char *request) {
    if (!text || !request) return false;
    char *req_copy = strdup(request);
    if (!req_copy) return false;
    bool found = false;
    char *saveptr;
    char *token = strtok_r(req_copy, " \t\n.,;:!?", &saveptr);
    while (token && !found) {
        if (strlen(token) > 3) {
            char *tc = strdup(text);
            char *tk = strdup(token);
            if (tc && tk) {
                for (char *c = tc; *c; c++) *c = (char)tolower((unsigned char)*c);
                for (char *c = tk; *c; c++) *c = (char)tolower((unsigned char)*c);
                if (strstr(tc, tk)) found = true;
            }
            free(tc);
            free(tk);
        }
        token = strtok_r(NULL, " \t\n.,;:!?", &saveptr);
    }
    free(req_copy);
    return found;
}

static int score_file(const ProjectFile *file, const char *request,
                      const char *project_root) {
    int score = 0;
    if (request && strstr(request, file->relative_path)) score += 50;
    if (file->is_source || file->is_header) score += 30;
    if (file->is_makefile) score += 20;
    if (file->is_test && request &&
        (strstr(request, "test") || strstr(request, "fix"))) score += 20;
    if (file->size_bytes < 5000)  score += 10;
    if (file->size_bytes > 50000) score -= 50;

    if (request) {
        char abs_path[ABS_PATH_MAX];
        build_abs_path(abs_path, sizeof(abs_path),
                       project_root, file->relative_path);
        FILE *f = fopen(abs_path, "r");
        if (f) {
            char *buf = malloc(MAX_FILE_READ + 1);
            if (buf) {
                size_t n = fread(buf, 1, MAX_FILE_READ, f);
                buf[n] = '\0';
                if (contains_keyword(buf, request)) score += 25;
                free(buf);
            }
            fclose(f);
        }
    }
    return score;
}

static char *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char *buf = malloc(MAX_FILE_READ + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t n = fread(buf, 1, MAX_FILE_READ, f);
    buf[n] = '\0';
    fclose(f);
    if (out_size) *out_size = n;
    return buf;
}

void context_pack_free(ContextPack *pack) {
    if (pack && pack->text) {
        free(pack->text);
        pack->text = NULL;
    }
}

bool context_pack_build(
    const char         *project_root,
    const ProjectIndex *index,
    const char         *request,
    size_t              max_bytes,
    ContextPack        *out)
{
    if (!project_root || !index || !out) return false;
    out->text = NULL; out->bytes_used = 0; out->files_included = 0;

    int    *scores = malloc(index->count * sizeof(int));
    size_t *order  = malloc(index->count * sizeof(size_t));
    if (!scores || !order) { free(scores); free(order); return false; }

    for (size_t i = 0; i < index->count; i++) {
        scores[i] = score_file(&index->files[i], request, project_root);
        order[i]  = i;
    }

    for (size_t i = 1; i < index->count; i++) {
        size_t key = order[i];
        int    ks  = scores[key];
        size_t j   = i;
        while (j > 0 && scores[order[j-1]] < ks) {
            order[j] = order[j-1]; j--;
        }
        order[j] = key;
    }

    size_t buf_cap = max_bytes + 4096;
    char  *buf     = malloc(buf_cap);
    if (!buf) { free(scores); free(order); return false; }

    size_t used = 0;
    int w = snprintf(buf + used, buf_cap - used, "PROJECT CONTEXT\n\n");
    if (w > 0) used += (size_t)w;

    for (size_t i = 0; i < index->count; i++) {
        const ProjectFile *file = &index->files[order[i]];
        if (scores[order[i]] < 0) continue;

        char abs_path[ABS_PATH_MAX];
        build_abs_path(abs_path, sizeof(abs_path),
                       project_root, file->relative_path);

        size_t file_size;
        char  *contents = read_file(abs_path, &file_size);
        if (!contents) continue;

        size_t entry_size = strlen(file->relative_path) + file_size + 32;
        if (used + entry_size > max_bytes) { free(contents); continue; }

        w = snprintf(buf + used, buf_cap - used,
                     "File: %s\n'''\n%s\n'''\n\n",
                     file->relative_path, contents);
        if (w > 0) used += (size_t)w;

        free(contents);
        out->files_included++;
    }

    free(scores); free(order);
    buf[used] = '\0';
    out->text = buf; out->bytes_used = used;
    return true;
}

// Created by sm4925 on 2026/6/14

#ifndef ARCHAGENT_TEST_HELPERS_H
#define ARCHAGENT_TEST_HELPERS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define TEST_ASSERT(cond) do { \
if (!(cond)) { \
fprintf(stderr, "FAIL: %s:%d: assertion failed: %s\n", __FILE__, __LINE__, #cond); \
return 1; \
} \
} while (0)

#if defined(__GNUC__) || defined(__clang__)
#define TEST_UNUSED __attribute__((unused))
#else
#define TEST_UNUSED
#endif

#define TEST_ASSERT_STR_CONTAINS(text, needle) do { \
if ((text) == NULL || strstr((text), (needle)) == NULL) { \
fprintf(stderr, "FAIL: %s:%d: expected text to contain '%s'\n", __FILE__, __LINE__, (needle)); \
return 1; \
} \
} while (0)

static TEST_UNUSED int test_write_file(const char *path, const char *contents) {
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    if (contents && fputs(contents, f) < 0) { fclose(f); return 0; }
    fclose(f);
    return 1;
}

static TEST_UNUSED int test_mkdir(const char *path) {
    return mkdir(path, 0755) == 0 || access(path, F_OK) == 0;
}

static TEST_UNUSED int test_make_temp_dir(char *out, size_t out_size, const char *prefix) {
    snprintf(out, out_size, "/tmp/%s_XXXXXX", prefix);
    return mkdtemp(out) != NULL;
}

static TEST_UNUSED int test_file_contains(const char *path, const char *needle) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    return strstr(buf, needle) != NULL;
}

#endif
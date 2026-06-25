// Created by st3125 on 2026/6/11
// ArchAgent IDE - Response parser

#include "response_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void parsed_response_free(ParsedResponse *parsed) {
    if (!parsed) return;
    free(parsed->plan_text);
    free(parsed->patch_text);
    free(parsed->tests_text);
    parsed->plan_text  = NULL;
    parsed->patch_text = NULL;
    parsed->tests_text = NULL;
}

// extract text between two section markers
// returns malloc'd string or NULL
static char *extract_section(const char *text,
                             const char *start_marker,
                             const char *end_marker) {
    const char *start = strstr(text, start_marker);
    if (!start) return NULL;
    start += strlen(start_marker);

    // skip leading newline
    if (*start == '\n') start++;

    const char *end;
    if (end_marker) {
        end = strstr(start, end_marker);
        if (!end) end = text + strlen(text);
    } else {
        end = text + strlen(text);
    }

    size_t len = (size_t)(end - start);
    char  *result = malloc(len + 1);
    if (!result) return NULL;

    memcpy(result, start, len);
    result[len] = '\0';
    return result;
}

// extract the diff block from between ``` markers
static char *extract_diff_block(const char *patch_section) {
    // look for ```diff or just ```
    const char *start = strstr(patch_section, "```diff");
    if (start) {
        start += strlen("```diff");
    } else {
        start = strstr(patch_section, "```");
        if (!start) return NULL;
        start += strlen("```");
    }

    // skip newline after opening fence
    if (*start == '\n') start++;

    const char *end = strstr(start, "```");
    if (!end) end = patch_section + strlen(patch_section);

    size_t len    = (size_t)(end - start);
    char  *result = malloc(len + 1);
    if (!result) return NULL;

    memcpy(result, start, len);
    result[len] = '\0';
    return result;
}

bool response_parse(const char *model_text, ParsedResponse *out) {
    if (!model_text || !out) return false;

    out->plan_text  = NULL;
    out->patch_text = NULL;
    out->tests_text = NULL;

    // extract PLAN section
    out->plan_text = extract_section(model_text, "PLAN:", "PATCH:");
    if (!out->plan_text) {
        fprintf(stderr, "Response parser: no PLAN section found\n");
        parsed_response_free(out);
        return false;
    }

    // extract PATCH section
    char *patch_section = extract_section(model_text, "PATCH:", "TESTS:");
    if (!patch_section) {
        fprintf(stderr, "Response parser: no PATCH section found\n");
        parsed_response_free(out);
        return false;
    }

    // extract diff block from within PATCH section
    out->patch_text = extract_diff_block(patch_section);
    free(patch_section);

    if (!out->patch_text || strlen(out->patch_text) == 0) {
        fprintf(stderr, "Response parser: empty or missing diff block\n");
        parsed_response_free(out);
        return false;
    }

    // extract TESTS section (optional — small models may omit it)
    out->tests_text = extract_section(model_text, "TESTS:", NULL);
    if (!out->tests_text) {
        out->tests_text = strdup("");
        if (!out->tests_text) {
            parsed_response_free(out);
            return false;
        }
    }

    return true;
}

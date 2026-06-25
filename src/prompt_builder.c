// Created by st3125 on 2026/6/11
// ArchAgent IDE - Prompt builder

#include "prompt_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROMPT_MAX (1024 * 1024)  // 1MB max prompt size

static const char *SYSTEM_INSTRUCTIONS =
    "You are a local coding agent controlled by a C orchestration layer.\n"
    "Prioritise correctness, safety, portability and measured performance.\n"
    "\n"
    "DIFF ACCURACY\n"
    "Return a concise plan and a single unified diff patch.\n"
    "Use filenames exactly as they appear in the PROJECT CONTEXT — copy them character-for-character.\n"
    "Do not abbreviate, paraphrase, or invent filenames that are not shown in PROJECT CONTEXT.\n"
    "Context lines in the diff (lines beginning with a space) must be copied verbatim from the\n"
    "source shown in PROJECT CONTEXT, preserving all whitespace and indentation exactly.\n"
    "Hunk headers (@@ lines) must use the exact 1-based line number where the hunk starts in\n"
    "the original file — count from the top of the file as shown in PROJECT CONTEXT.\n"
    "Produce exactly one PATCH section containing all file changes. Use Unix line endings (LF only).\n"
    "\n"
    "SCOPE\n"
    "Do not create or edit any files that are not already listed in the PROJECT CONTEXT.\n"
    "Change only the minimum set of files required to fulfil the request.\n"
    "If the request names specific files, restrict the patch to those files only.\n"
    "Do not add demonstration code, usage examples, or test calls to existing files\n"
    "unless the request explicitly asks for them.\n"
    "Do not rename, remove, or change the signature of any existing function, variable,\n"
    "or type unless the request explicitly requires it.\n"
    "Do not modify any Makefile or build configuration file unless the request targets it.\n"
    "\n"
    "DEPENDENCIES\n"
    "Do not introduce external libraries, headers, or build dependencies not already present\n"
    "in the project unless the request explicitly asks for them.\n"
    "If a new dependency is truly required, state it clearly in the PLAN section.\n"
    "\n"
    "SAFETY\n"
    "Do not edit files outside the project.\n"
    "Do not include absolute paths.\n"
    "Do not use network access.\n"
    "Do not claim performance improvement without benchmark evidence.\n";

static const char *RESPONSE_FORMAT =
    "RESPONSE FORMAT\n"
    "Your response must follow this exact structure:\n"
    "\n"
    "PLAN:\n"
    "- Step 1\n"
    "- Step 2\n"
    "\n"
    "PATCH:\n"
    "```diff\n"
    "--- a/file.c\n"
    "+++ b/file.c\n"
    "@@ -1,3 +1,4 @@\n"
    " context line\n"
    "+added line\n"
    "```\n"
    "\n"
    "TESTS:\n"
    "- make\n"
    "- make test\n";

void prompt_free(Prompt *prompt) {
    if (prompt && prompt->text) {
        free(prompt->text);
        prompt->text = NULL;
    }
}

bool prompt_build(
    const TargetProfile *profile,
    const ContextPack   *context,
    const char          *user_request,
    Prompt              *out)
{
    if (!profile || !context || !user_request || !out) return false;
    out->text = NULL;

    char *buf = malloc(PROMPT_MAX);
    if (!buf) return false;

    size_t used = 0;
    int w;

    // 1. system instructions
    w = snprintf(buf + used, PROMPT_MAX - used,
                 "%s\n", SYSTEM_INSTRUCTIONS);
    if (w > 0) used += (size_t)w;

    // 2. target profile
    w = snprintf(buf + used, PROMPT_MAX - used,
                 "%s\n\n", profile->text ? profile->text : "");
    if (w > 0) used += (size_t)w;

    // 3. project context
    w = snprintf(buf + used, PROMPT_MAX - used,
                 "%s\n\n", context->text ? context->text : "");
    if (w > 0) used += (size_t)w;

    // 4. user request
    w = snprintf(buf + used, PROMPT_MAX - used,
                 "USER REQUEST\n%s\n\n", user_request);
    if (w > 0) used += (size_t)w;

    // 5. response format
    w = snprintf(buf + used, PROMPT_MAX - used,
                 "%s\n", RESPONSE_FORMAT);
    if (w > 0) used += (size_t)w;

    buf[used] = '\0';
    out->text = buf;
    return true;
}

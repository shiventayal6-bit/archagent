// Created by st3125 on 2026/6/11
// ArchAgent IDE - Prompt builder

#ifndef ARCHAGENT_PROMPT_BUILDER_H
#define ARCHAGENT_PROMPT_BUILDER_H

#include "target_profile.h"
#include "context_packer.h"
#include <stdbool.h>

typedef struct {
    char *text;
} Prompt;

// assemble the full prompt from profile, context and request
bool prompt_build(
    const TargetProfile *profile,
    const ContextPack   *context,
    const char          *user_request,
    Prompt              *out
);

// free memory allocated by prompt_build
void prompt_free(Prompt *prompt);

#endif // ARCHAGENT_PROMPT_BUILDER_H

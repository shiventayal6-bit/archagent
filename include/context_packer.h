// Created by st3125 on 2026/6/11
// ArchAgent IDE - Context packer

#ifndef ARCHAGENT_CONTEXT_PACKER_H
#define ARCHAGENT_CONTEXT_PACKER_H

#include "project_scanner.h"
#include <stdbool.h>
#include <stddef.h>

// the packed context to send to the AI
typedef struct {
    char  *text;
    size_t bytes_used;
    size_t files_included;
} ContextPack;

// select relevant files and pack them into a text block
// max_bytes limits how much context we include
bool context_pack_build(
    const char         *project_root,
    const ProjectIndex *index,
    const char         *request,
    size_t              max_bytes,
    ContextPack        *out
);

// free memory allocated by context_pack_build
void context_pack_free(ContextPack *pack);

#endif // ARCHAGENT_CONTEXT_PACKER_H

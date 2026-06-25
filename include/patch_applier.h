// Created by st3125 on 2026/6/11
// ArchAgent IDE - Patch applier

#ifndef ARCHAGENT_PATCH_APPLIER_H
#define ARCHAGENT_PATCH_APPLIER_H

#include "diff_parser.h"
#include <stdbool.h>

typedef struct {
    bool ok;
    char message[1024];
} PatchApplyReport;

// apply a parsed diff to files inside sandbox_root
// returns true only if every file's hunks applied cleanly
bool patch_apply_to_sandbox(
    const ParsedDiff *diff,
    const char       *sandbox_root,
    PatchApplyReport *report
);

#endif // ARCHAGENT_PATCH_APPLIER_H

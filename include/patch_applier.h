// ArchAgent — Patch applier
// Applies a parsed unified diff to files inside a sandbox directory.
// Uses fuzzy context-line matching to tolerate minor whitespace drift
// or off-by-one line numbers from the model.

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

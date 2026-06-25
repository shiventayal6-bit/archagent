// Created by st3125 on 2026/6/11
// ArchAgent IDE - Patch validator

#ifndef ARCHAGENT_PATCH_VALIDATOR_H
#define ARCHAGENT_PATCH_VALIDATOR_H

#include "diff_parser.h"
#include <stdbool.h>

typedef struct {
    bool ok;
    char message[1024];
} ValidationReport;

// validate that a parsed diff is safe to apply
// checks every file path against unsafe patterns
bool patch_validate(
    const ParsedDiff *diff,
    const char       *project_root,
    ValidationReport *report
);

#endif // ARCHAGENT_PATCH_VALIDATOR_H

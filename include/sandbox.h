// ArchAgent — Sandbox manager
// Creates an isolated session directory and copies the project into it.
// Session IDs are "YYYYMMDD_HHMMSS_<pid>" to prevent collisions.

#ifndef ARCHAGENT_SANDBOX_H
#define ARCHAGENT_SANDBOX_H

#include <stdbool.h>
#include <limits.h>

typedef struct {
    char session_id[64];
    char session_dir[PATH_MAX];
    char sandbox_root[PATH_MAX];
} Sandbox;

// create a new session directory under <project_root>/.archagent/sessions/<id>/
// also creates the sandbox/ subdirectory and the empty audit log files
bool sandbox_create(const char *project_root, const char *audit_dir, Sandbox *out);

// copy the project files into the sandbox, skipping .git, .archagent etc
bool sandbox_copy_project(const char *project_root, const Sandbox *sandbox);

#endif // ARCHAGENT_SANDBOX_H

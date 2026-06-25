// ArchAgent — Audit logger
// Writes per-session artefacts and a JSONL event log to the session directory.

#ifndef ARCHAGENT_AUDIT_LOGGER_H
#define ARCHAGENT_AUDIT_LOGGER_H

#include "sandbox.h"
#include <stdbool.h>

// append one JSON line to events.jsonl in the session directory
// event_json should be a complete JSON object without trailing newline,
// e.g. "{\"event\":\"session_start\",\"project\":\"demo_projects/calculator\"}"
bool audit_log_event(const Sandbox *sandbox, const char *event_json);

// write text content to a named file in the session directory
// (used for profile.txt, prompt.txt, patch.diff etc)
bool audit_write_file(const Sandbox *sandbox, const char *filename, const char *content);

#endif // ARCHAGENT_AUDIT_LOGGER_H

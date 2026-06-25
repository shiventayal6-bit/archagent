// ArchAgent — Ollama backend
// Calls the Ollama HTTP API (POST /api/generate) to run local models.
// Parses the JSONL streaming response; handles connection refused,
// HTTP errors, and timeouts with descriptive error messages.
// Default model: ARCHAGENT_DEFAULT_OLLAMA_MODEL.

#ifndef ARCHAGENT_OLLAMA_BACKEND_H
#define ARCHAGENT_OLLAMA_BACKEND_H

#include "mock_backend.h"

#include <stdbool.h>

#define ARCHAGENT_DEFAULT_OLLAMA_MODEL "qwen2.5-coder:7b"

bool ollama_backend_generate(const char *prompt,
                             const char *model_name,
                             int timeout_seconds,
                             ModelResponse *out);

#endif // ARCHAGENT_OLLAMA_BACKEND_H
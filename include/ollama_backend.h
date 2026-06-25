// Created by sm4925 on 2026/6/14

#ifndef ARCHAGENT_OLLAMA_BACKEND_H
#define ARCHAGENT_OLLAMA_BACKEND_H

#include "mock_backend.h"

#include <stdbool.h>

#define ARCHAGENT_DEFAULT_OLLAMA_MODEL "qwen2.5-coder:7b"

bool ollama_backend_generate(const char *prompt,
                             const char *model_name,
                             int timeout_seconds,
                             ModelResponse *out);

#endif
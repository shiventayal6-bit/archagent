// Created by st3125 on 2026/6/14
// ArchAgent IDE - llama.cpp backend

#ifndef ARCHAGENT_LLAMA_BACKEND_H
#define ARCHAGENT_LLAMA_BACKEND_H

#include "mock_backend.h"

#include <stdbool.h>

// call llama-cli with the given prompt and model
// - returns false with out->text == NULL and out->exit_code == 127 if
//   llama-cli is not found on PATH
// - returns false with out->text == NULL and out->exit_code == 2 if
//   model_path is NULL or does not exist
// - otherwise runs "llama-cli -m <model_path> -p <prompt>" with the given
//   timeout and captures stdout as out->text
bool llama_backend_generate(const char *prompt, const char *model_path,
                            int timeout_seconds, ModelResponse *out);

#endif // ARCHAGENT_LLAMA_BACKEND_H

// Created by st3125 on 2026/6/11
// ArchAgent IDE - Mock backend

#ifndef ARCHAGENT_MOCK_BACKEND_H
#define ARCHAGENT_MOCK_BACKEND_H

#include <stdbool.h>

typedef struct {
    char *text;
    int   exit_code;
    bool  timed_out;
} ModelResponse;

// call the mock backend with a prompt and request
// returns a fixed response based on the request content
bool mock_backend_generate(const char *request, ModelResponse *out);

// free memory allocated by mock_backend_generate
void model_response_free(ModelResponse *response);

#endif // ARCHAGENT_MOCK_BACKEND_H

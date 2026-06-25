// ArchAgent — Response parser
// Extracts three delimited sections from a model response:
// <PLAN> (explanation), <PATCH> (unified diff), <TESTS> (verification commands).

#ifndef ARCHAGENT_RESPONSE_PARSER_H
#define ARCHAGENT_RESPONSE_PARSER_H

#include <stdbool.h>

typedef struct {
    char *plan_text;
    char *patch_text;
    char *tests_text;
} ParsedResponse;

// parse the model response into plan, patch and tests sections
// returns true on success, false if response is invalid
bool response_parse(const char *model_text, ParsedResponse *out);

// free memory allocated by response_parse
void parsed_response_free(ParsedResponse *parsed);

#endif // ARCHAGENT_RESPONSE_PARSER_H

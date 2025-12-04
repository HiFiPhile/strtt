/*
 * C port of InputParser - Simple command line argument parser
 */

#ifndef _INPUT_PARSER_C_H
#define _INPUT_PARSER_C_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char **tokens;
    int count;
} input_parser_t;

input_parser_t* input_parser_create(int argc, char **argv);
void input_parser_destroy(input_parser_t *parser);
bool input_parser_cmd_option_exists(input_parser_t *parser, const char *option);
const char* input_parser_get_cmd_option(input_parser_t *parser, const char *option);

#ifdef __cplusplus
}
#endif

#endif // _INPUT_PARSER_C_H

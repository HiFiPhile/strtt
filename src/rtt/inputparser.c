/*
 * C port of InputParser - Simple command line argument parser
 */

#include <stdlib.h>
#include <string.h>
#include "inputparser.h"

input_parser_t* input_parser_create(int argc, char **argv) {
    input_parser_t *parser = (input_parser_t*)malloc(sizeof(input_parser_t));
    if (!parser) return NULL;

    parser->count = argc - 1;
    parser->tokens = NULL;

    if (parser->count > 0) {
        parser->tokens = (char**)malloc(parser->count * sizeof(char*));
        if (!parser->tokens) {
            free(parser);
            return NULL;
        }

        for (int i = 0; i < parser->count; i++) {
            parser->tokens[i] = strdup(argv[i + 1]);
            if (!parser->tokens[i]) {
                for (int j = 0; j < i; j++) {
                    free(parser->tokens[j]);
                }
                free(parser->tokens);
                free(parser);
                return NULL;
            }
        }
    }

    return parser;
}

void input_parser_destroy(input_parser_t *parser) {
    if (!parser) return;

    if (parser->tokens) {
        for (int i = 0; i < parser->count; i++) {
            if (parser->tokens[i]) {
                free(parser->tokens[i]);
            }
        }
        free(parser->tokens);
    }

    free(parser);
}

bool input_parser_cmd_option_exists(input_parser_t *parser, const char *option) {
    if (!parser || !option) return false;

    for (int i = 0; i < parser->count; i++) {
        if (strcmp(parser->tokens[i], option) == 0) {
            return true;
        }
    }

    return false;
}

const char* input_parser_get_cmd_option(input_parser_t *parser, const char *option) {
    if (!parser || !option) return "";

    for (int i = 0; i < parser->count; i++) {
        if (strcmp(parser->tokens[i], option) == 0) {
            if (i + 1 < parser->count) {
                return parser->tokens[i + 1];
            }
        }
    }

    return "";
}

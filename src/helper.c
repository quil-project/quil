/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

// NOTE: This file holds all the helper functions that helps other processes run.

#include "../include/error.h"
#include "../include/lexer.h"
#include "../include/mode.h"
#include "../include/parser.h"
#include <stdbool.h>
#include <string.h>

// code generation mode
bool ENGINE_MODE = false;

// NOTE: The below function is from include/mode.h
bool check_program_mode(token_list *list) {
        // The size will be 4 because the newline character is included in the list
        // The list would be something like ["@", "for", "engine", '\n']
        if (!(list->size == 4 && list->tokens[0].type == TOKEN_ATSIGN)) {
                return false; // return engine mode which is false
        }
        if (!(list->tokens[1].type == TOKEN_ID && list->tokens[2].type == TOKEN_ID)) {
                return false; // return engine mode which is false
        }
        if (strcmp("for", list->tokens[1].value) == 0 && strcmp("engine", list->tokens[2].value) == 0) {
                ENGINE_MODE = true;
        }

        return ENGINE_MODE;
}
// ===============================================

// NOTE: The below functions are from include/lexer.h
void token_list_init(token_list *list) {
        list->size = 0;
        list->capacity = 16;
        list->tokens = malloc(list->capacity * sizeof(token));
}
void token_list_add(token_list *list, token t) {
        if (list->size >= list->capacity) {
                list->capacity *= 2;
                list->tokens = realloc(list->tokens, list->capacity * sizeof(token));
        }
        list->tokens[list->size++] = t;
}
void token_list_free(token_list *list) {
        for (size_t i = 0; i < list->size; i++) {
                if (list->tokens[i].value) {
                        free(list->tokens[i].value);
                        list->tokens[i].value = NULL;
                }
        }
        free(list->tokens);
        list->tokens = NULL;
        list->size = 0;
        list->capacity = 0;
}
// ==================================================

// NOTE: The below functions are from include/parser.h
// Peeks/takes a looks at the current token without moving to the next token.
token peek(Parser *parser) {
        if (parser->current >= parser->tokens->size) {
                return parser->tokens->tokens[parser->tokens->size - 1];
        }
        return parser->tokens->tokens[parser->current];
}
// moves to the next token
token advance(Parser *parser) {
        if (parser->current < parser->tokens->size) {
                parser->current++;
        }
        return parser->tokens->tokens[parser->current - 1];
}
// checks the current token type
bool check(Parser *parser, tokenType type) {
        return peek(parser).type == type;
}
// checks if current token matches with another token and moves on
bool match(Parser *parser, tokenType type) {
        if (check(parser, type)) {
                advance(parser);
                return true;
        }
        // return false if it dosent match and stays at the same place (dosent move on)
        return false;
}
// returns a human-readable description of the current token for error messages
const char *peek_display(Parser *parser) {
        token t = peek(parser);
        if (t.value) {
                return t.value;
        }
        static char buf[64];
        switch (t.type) {
        case TOKEN_INUM:
                snprintf(buf, sizeof(buf), "%lld", (long long)t.int_value);
                break;
        case TOKEN_FNUM:
                snprintf(buf, sizeof(buf), "%g", t.float_value);
                break;
        case TOKEN_NLINE:
                return "newline";
        case TOKEN_EOF:
                return "end of file";
        default:
                return lexer_token_type_to_string(t.type);
        }
        return buf;
}
// checks for error and throws error messages
// 'expected' is a description of the token that was expected (e.g. "')'")
token consume(Parser *parser, tokenType type, const char *expected) {
        if (check(parser, type)) {
                return advance(parser);
        }
        token found = peek(parser);
        quil_expected_at(STAGE_PARSER, found.line, found.col, expected, peek_display(parser));
}
void consume_end_of_statement(Parser *parser) {
        if (match(parser, TOKEN_SEMICOLON) || match(parser, TOKEN_NLINE)) {
                return;
        }
        // If the next line is EOF its the end of statement, check for EOF
        if (check(parser, TOKEN_EOF)) {
                return;
        }

        token found = peek(parser);
        quil_expected_at(STAGE_PARSER, found.line, found.col, "';' or newline after statement", peek_display(parser));
}
// ===================================================

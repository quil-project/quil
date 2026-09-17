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

#include "../include/lexer.h"
#include "../include/mode.h"
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

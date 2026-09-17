/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

#include "../../include/lexer.h"
#include <stdlib.h>

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

/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

#ifndef SEMA_H
#define SEMA_H

#include "_hashmap.h"
#include "ast.h"
#include "lexer.h"
#include <stddef.h>

typedef struct {
        HashMap **frames;      // stack of symbol tables
        HashMap *functions;    // table for function type and name
        HashMap *types;        // qualified struct name -> StructDef*
        HashMap *const_vars;   // name -> (void*)1 for const variables
        size_t frame_count;    // number of scopes currently open
        size_t frame_capacity; // allocated slots
        char *cur_ns;          // current namespace/scope
} SemAnalyzer;

// aggregate type layout (offsets mirror feather addm() padding)
typedef struct {
        char **field_names;
        char **field_types; // resolved (qualified) type names
        int *field_offsets;
        int field_count;
        int size;
        int align; // log2 alignment
} StructDef;

// function signatures
typedef struct {
        // we do not need to hold name in the signature, the hashmap key will be the name
        char *return_type;
        char **param_types;
        size_t param_count;
        bool is_public;
        bool is_extern;
} funcSig;

// entry point
void semantic_analyze(ASTnode *program);

#endif // !SEMA_H

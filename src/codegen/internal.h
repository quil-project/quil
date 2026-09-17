/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

#ifndef QUIL_CODEGEN_INTERNAL_H
#define QUIL_CODEGEN_INTERNAL_H

#include "../../feather/config.h"
#include "../../include/error.h"
#include "../../include/ssagen.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// would be defined in feather/main.c — defined in ssagen.c
extern Target T;
extern int optlevel;
extern char debug[];

typedef struct {
        char **field_names;
        int *field_offsets;
        int field_count;
        int size;
        int align; // log2 alignment
} SsaStruct;

typedef struct {
        ILBuilder *ilb;
        HashMap *slots;
        HashMap *externs;
        HashMap *struct_types;
        IlModule *mod;
        char *cur_ns;
        Blk *break_target;
        Blk *continue_target;
} Ssagen;

// types.c
bool is_unsigned_type(const char *t);
Ref promote_kw_to_kl(Ssagen *s, ASTnode *node, Ref r);
int quil_to_cls(const char *t);
int elem_size_of(const char *t);
void emit_store_elem(Ssagen *s, const char *t, Ref v, Ref addr);
Ref emit_load_elem(Ssagen *s, const char *t, Ref addr);
int ssa_align_of(Ssagen *s, const char *t);
bool ssa_is_struct(Ssagen *s, const char *t);
int ssa_type_size(Ssagen *s, const char *t);

// stmt.c / expr.c
Ref emit_obj_addr(Ssagen *s, ASTnode *obj);
Ref emit_expr(Ssagen *s, ASTnode *n);
void emit_stmt(Ssagen *s, ASTnode *n);

// ssagen.c
void ssa_struct_free(void *p);
char *ssa_struct_qname(Ssagen *s, const char *name);
void collect_struct(Ssagen *s, ASTnode *def);
char *mangle(const char *qname);
void emit_func(Ssagen *s, ASTnode *fndef);

#endif // QUIL_CODEGEN_INTERNAL_H

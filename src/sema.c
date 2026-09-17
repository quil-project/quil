/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

/* Semaintic Analysis + Symbol table */

#include "../include/sema.h"
#include "../include/error.h"
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- HELPERS ---
// - scope stack -
static void sem_push_scope(SemAnalyzer *a) {
        // dynamic sizing
        if (a->frame_count >= a->frame_capacity) {
                a->frame_capacity = a->frame_capacity ? a->frame_capacity * 2 : 8;
                HashMap **tmp = realloc(a->frames, a->frame_capacity * sizeof(HashMap *));
                if (!tmp) {
                        quil_error(STAGE_INTERNAL, ERR_ALLOC_FAILED, NULL);
                }
                a->frames = tmp;
        }
        a->frames[a->frame_count++] = hashmap_create(hm_hash_str, hm_eq_str, free, free);
}
static void sem_pop_scope(SemAnalyzer *a) {
        if (a->frame_count == 0) {
                return; // for stack underflow
        }
        hashmap_free(a->frames[--a->frame_count]);
}
// - declare / resolve -
static void sem_declare(SemAnalyzer *a, const char *name, const char *type, int line, int col) {
        // previous scope / frame
        HashMap *frame = a->frames[a->frame_count - 1];
        char *k = strdup(name);
        char *v = strdup(type);
        // returns false if key exists
        if (!hashmap_insert(frame, k, v)) {
                free(k);
                free(v);
                quil_error_at(STAGE_SEMANTIC, ERR_REDECLARED_VAR, line, col, name);
        }
}
static const char *sem_resolve(SemAnalyzer *a, const char *name) {
        // iterate from the top scope to see if the veriable exists
        for (int i = (int)a->frame_count - 1; i >= 0; i--) {
                bool found;
                const char *type = hashmap_get(a->frames[i], name, &found);
                if (found) {
                        return type;
                }
        }
        return NULL;
}
// full C type for a symbol, e.g. "char *"
static char *sem_fulltype(const char *type_name, const char *modifiers, const char *element_type) {
        (void)element_type; // vec removed
        char *base;
        if (strcmp(type_name, "string") == 0) base = strdup("char *");
        else base = strdup(type_name);
        if (modifiers == NULL) {
                return base;
        }
        char *out = malloc(strlen(modifiers) + strlen(base) + 2);
        sprintf(out, "%s %s", modifiers, base);
        free(base);
        return out;
}

// free StructDef
static void structdef_free(void *p) {
        StructDef *sd = (StructDef *)p;
        if (!sd) {
                return;
        }
        for (int i = 0; i < sd->field_count; i++) {
                free(sd->field_names[i]);
                free(sd->field_types[i]);
        }
        free(sd->field_names);
        free(sd->field_types);
        free(sd->field_offsets);
        free(sd);
}

// true for the 12 primitive type names (parse_type() in src/parser.c)
static bool sem_is_primitive(const char *t) {
        return !strcmp(t, "int8") || !strcmp(t, "int16") || !strcmp(t, "int32") ||
               !strcmp(t, "int64") || !strcmp(t, "uint8") || !strcmp(t, "uint16") ||
               !strcmp(t, "uint32") || !strcmp(t, "uint64") || !strcmp(t, "float32") ||
               !strcmp(t, "float64") || !strcmp(t, "char") || !strcmp(t, "string") ||
               !strcmp(t, "bool");
}

// byte size of a type (primitives + pointers + registered structs)
static int sem_size_of(SemAnalyzer *a, const char *t) {
        if (!strcmp(t, "int8") || !strcmp(t, "uint8") || !strcmp(t, "char") || !strcmp(t, "bool")) return 1;
        if (!strcmp(t, "int16") || !strcmp(t, "uint16")) return 2;
        if (!strcmp(t, "int32") || !strcmp(t, "uint32") || !strcmp(t, "float32")) return 4;
        if (!strcmp(t, "int64") || !strcmp(t, "uint64") || !strcmp(t, "float64") || !strcmp(t, "string")) return 8;
        if (t[strlen(t) - 1] == '*') return 8; // all pointers are l
        bool found = false;
        StructDef *sd = hashmap_get(a->types, t, &found);
        if (found) return sd->size;
        return 0; // unknown
}

// log2 alignment (matches feather addm(): b=0 h=1 w/s=2 l/d=3)
static int sem_align_of(SemAnalyzer *a, const char *t) {
        if (!strcmp(t, "int8") || !strcmp(t, "uint8") || !strcmp(t, "char") || !strcmp(t, "bool")) return 0;
        if (!strcmp(t, "int16") || !strcmp(t, "uint16")) return 1;
        if (!strcmp(t, "int32") || !strcmp(t, "uint32") || !strcmp(t, "float32")) return 2;
        if (!strcmp(t, "int64") || !strcmp(t, "uint64") || !strcmp(t, "float64") || !strcmp(t, "string")) return 3;
        if (t[strlen(t) - 1] == '*') return 3;
        bool found = false;
        StructDef *sd = hashmap_get(a->types, t, &found);
        if (found) return sd->align;
        return 2;
}

// look up a struct field: returns field type or NULL, sets *out_off when found
static const char *sem_struct_field(SemAnalyzer *a, const char *struct_type, const char *member, int *out_off) {
        if (!struct_type) return NULL;
        bool found = false;
        StructDef *sd = hashmap_get(a->types, struct_type, &found);
        if (!found) return NULL;
        for (int i = 0; i < sd->field_count; i++) {
                if (!strcmp(sd->field_names[i], member)) {
                        if (out_off) *out_off = sd->field_offsets[i];
                        return sd->field_types[i];
                }
        }
        return NULL;
}

// resolve a written type to its qualified table name (cur_ns::T preferred, then T)
static char *sem_resolve_type(SemAnalyzer *a, const char *t) {
        if (sem_is_primitive(t) || t[strlen(t) - 1] == '*') return strdup(t);
        if (a->cur_ns) {
                size_t len = strlen(a->cur_ns) + 2 + strlen(t) + 1;
                char *q = malloc(len);
                snprintf(q, len, "%s::%s", a->cur_ns, t);
                bool found = false;
                hashmap_get(a->types, q, &found);
                if (found) return q;
                free(q);
        }
        bool found = false;
        hashmap_get(a->types, t, &found);
        if (found) return strdup(t);
        return NULL; // unknown
}

// free funcSig struct
static void funcSig_free(void *p) {
        funcSig *fs = (funcSig *)p;
        if (!fs) {
                return;
        }
        free(fs->return_type);
        for (size_t i = 0; i < fs->param_count; i++) {
                free(fs->param_types[i]);
        }
        free(fs->param_types);
        free(fs);
}

// - Walker -
static void sem_analyze_node(SemAnalyzer *a, ASTnode *node) {
        switch (node->type) {

        // ---- Program & blocks ----
        case NODE_PROGRAM:
                sem_push_scope(a);
                for (int i = 0; i < node->data.program.count; i++) {
                        ASTnode *stmt = node->data.program.statements[i];
                        // file scope may only hold declarations/directives/namespaces/structs;
                        // executables must live inside 'fn main()'
                        if (stmt->type != NODE_FUNC_DEF && stmt->type != NODE_VAR_DECL &&
                            stmt->type != NODE_IMPORT && stmt->type != NODE_DIRECTIVE &&
                            stmt->type != NODE_NAMESPACE && stmt->type != NODE_STRUCT_DEF) {
                                quil_error_at(STAGE_SEMANTIC, ERR_TOP_LEVEL_STMT, stmt->line, stmt->col, NULL);
                        }
                        sem_analyze_node(a, stmt);
                }
                sem_pop_scope(a);
                break;
        case NODE_BLOCK:
                sem_push_scope(a);
                for (int i = 0; i < node->data.blocks.count; i++) {
                        sem_analyze_node(a, node->data.blocks.statements[i]);
                }
                sem_pop_scope(a);
                break;

        // ---- Literals (intrinsic types: float literals are float64 like C doubles) ----
        case NODE_INT_LITERAL: {
                int64_t v = node->data.int_literal.value;
                uint64_t uv = (uint64_t)v;
                if (uv > (uint64_t)INT64_MAX) node->resolved_type = strdup("uint64");
                else if (v >= INT32_MIN && v <= INT32_MAX) node->resolved_type = strdup("int32");
                else node->resolved_type = strdup("int64");
                break;
        }
        case NODE_FLOAT_LITERAL: node->resolved_type = strdup("float64"); break;
        case NODE_STRING_LITERAL: node->resolved_type = strdup("char *"); break;
        case NODE_BOOL_LITERAL: node->resolved_type = strdup("bool"); break;
        case NODE_CHAR_LITERAL: node->resolved_type = strdup("char"); break;
        case NODE_LIST_LITERAL: {
                for (int i = 0; i < node->data.list_literal.count; i++) {
                        sem_analyze_node(a, node->data.list_literal.elements[i]);
                }
                break;
        }

        // ---- Identifiers & element access ----
        case NODE_IDENTIFIER: {
                const char *type = sem_resolve(a, node->data.identifier.name);
                if (!type) {
                        quil_error_at(STAGE_SEMANTIC, ERR_UNDECLARED_VAR, node->line, node->col, node->data.identifier.name);
                }
                node->resolved_type = strdup(type);
                break;
        }
        case NODE_ARRAY_ACCESS: {
                const char *type = sem_resolve(a, node->data.array_access.name);
                if (!type) {
                        quil_error_at(STAGE_SEMANTIC, ERR_UNDECLARED_VAR, node->line, node->col, node->data.array_access.name);
                }
                node->resolved_type = strdup(type);
                sem_analyze_node(a, node->data.array_access.index);
                break;
        }
        case NODE_MEMBER_ACCESS: {
                sem_analyze_node(a, node->data.member_access.object);
                const char *obj_type = node->data.member_access.object->resolved_type;
                if (node->data.member_access.arg_count == 0 && obj_type) {
                        // struct field read: look up the field type + offset
                        int off = -1;
                        const char *ft = sem_struct_field(a, obj_type, node->data.member_access.member, &off);
                        if (ft) {
                                node->resolved_type = strdup(ft);
                                node->data.member_access.field_offset = off;
                                break;
                        }
                        // struct type but unknown field
                        if (obj_type) {
                                bool is_struct = false;
                                hashmap_get(a->types, obj_type, &is_struct);
                                if (is_struct) {
                                        quil_error_at(STAGE_SEMANTIC, ERR_UNKNOWN, node->line, node->col, node->data.member_access.member);
                                }
                        }
                }
                if (node->data.member_access.arg_count > 0) {
                        // no method system (vec methods removed with vec); method calls are rejected
                        // until methods return as stdlib functions
                        quil_error_at(STAGE_SEMANTIC, ERR_UNKNOWN, node->line, node->col, node->data.member_access.member);
                }
                for (int i = 0; i < node->data.member_access.arg_count; i++) {
                        sem_analyze_node(a, node->data.member_access.args[i]);
                }
                // scalar for now (method calls return void; property reads are numeric)
                node->resolved_type = strdup("int32");
                break;
        }

        // ---- Expressions (binary / unary / ternary) ----
        case NODE_BINARY_EXPRESSION: {
                sem_analyze_node(a, node->data.binary_expression.left);
                sem_analyze_node(a, node->data.binary_expression.right);
                tokenType bop = node->data.binary_expression.op;
                // comparisons and logicals produce bool (Kw)
                if (bop == TOKEN_EEQUAL || bop == TOKEN_NEQUAL || bop == TOKEN_LABRACKET ||
                    bop == TOKEN_RABRACKET || bop == TOKEN_LEQUAL || bop == TOKEN_GEQUAL ||
                    bop == TOKEN_AND || bop == TOKEN_OR) {
                        node->resolved_type = strdup("bool");
                        break;
                }
                // arithmetic: promote float64 > float32 > int64/uint64 > left type
                const char *lt = node->data.binary_expression.left->resolved_type;
                const char *rt = node->data.binary_expression.right->resolved_type;
                if ((lt && !strcmp(lt, "float64")) || (rt && !strcmp(rt, "float64"))) node->resolved_type = strdup("float64");
                else if ((lt && !strcmp(lt, "float32")) || (rt && !strcmp(rt, "float32"))) node->resolved_type = strdup("float32");
                else if ((lt && (!strcmp(lt, "int64") || !strcmp(lt, "uint64"))) ||
                         (rt && (!strcmp(rt, "int64") || !strcmp(rt, "uint64")))) {
                        node->resolved_type = strdup(lt && (!strcmp(lt, "int64") || !strcmp(lt, "uint64")) ? lt : rt);
                } else node->resolved_type = strdup(lt ? lt : "int32");
                break;
        }
        case NODE_UNARY_EXPRESSION: {
                sem_analyze_node(a, node->data.unary_expression.left);
                // !a -> bool, -a -> operand type
                if (node->data.unary_expression.op == TOKEN_EXCLAMATION) node->resolved_type = strdup("bool");
                else {
                        const char *ut = node->data.unary_expression.left->resolved_type;
                        node->resolved_type = strdup(ut ? ut : "int32");
                }
                break;
        }
        case NODE_TERNARY_EXPRESSION: {
                sem_analyze_node(a, node->data.ternary_expression.condition);
                sem_analyze_node(a, node->data.ternary_expression.then_expr);
                sem_analyze_node(a, node->data.ternary_expression.else_expr);
                // result type follows the then-branch (mirrors old codegen_specifier)
                const char *tt = node->data.ternary_expression.then_expr->resolved_type;
                node->resolved_type = strdup(tt ? tt : "int32");
                break;
        }

        // ---- Declarations & assignment ----
        case NODE_VAR_DECL: {
                char *type = NULL;
                char *rtype = NULL;
                if (node->data.var_decl.is_inferred) {
                        if (!node->data.var_decl.value) {
                                quil_error_at(STAGE_SEMANTIC, ERR_UNDECLARED_TYPE, node->line, node->col, ":= requires initializer");
                        }
                        sem_analyze_node(a, node->data.var_decl.value);
                        const char *vt = node->data.var_decl.value->resolved_type;
                        if (!vt) quil_error_at(STAGE_SEMANTIC, ERR_UNDECLARED_TYPE, node->line, node->col, "cannot infer type");
                        node->resolved_type = strdup(vt);
                        type = strdup(vt);
                        // also set type_name for codegen (use inferred type)
                        free(node->data.var_decl.type_name);
                        node->data.var_decl.type_name = strdup(vt);
                        sem_declare(a, node->data.var_decl.name, type, node->line, node->col);
                        free(type);
                } else {
                        rtype = sem_resolve_type(a, node->data.var_decl.type_name);
                        if (!rtype) {
                                quil_error_at(STAGE_SEMANTIC, ERR_UNDECLARED_TYPE, node->line, node->col, node->data.var_decl.type_name);
                        }
                        type = sem_fulltype(rtype, node->data.var_decl.modifiers, NULL);
                        free(rtype);
                        node->resolved_type = strdup(type);
                        sem_declare(a, node->data.var_decl.name, type, node->line, node->col);
                        free(type);
                        if (node->data.var_decl.value) {
                                sem_analyze_node(a, node->data.var_decl.value);
                        }
                }
                if (node->data.var_decl.is_const) {
                        char *k = strdup(node->data.var_decl.name);
                        hashmap_put(a->const_vars, k, (void*)1);
                }
                break;
        }
        case NODE_ASSIGN: {
                if (node->data.assign.is_member) {
                        // obj.field = v: analyze object, resolve field type + offset
                        sem_analyze_node(a, node->data.assign.obj);
                        const char *obj_type = node->data.assign.obj->resolved_type;
                        int off = -1;
                        const char *ft = sem_struct_field(a, obj_type, node->data.assign.member, &off);
                        if (!ft) {
                                quil_error_at(STAGE_SEMANTIC, ERR_UNKNOWN, node->line, node->col, node->data.assign.member);
                        }
                        node->resolved_type = strdup(ft);
                        node->data.assign.field_offset = off;
                        sem_analyze_node(a, node->data.assign.value);
                        break;
                }
                const char *type = sem_resolve(a, node->data.assign.name);
                if (!type) {
                        quil_error_at(STAGE_SEMANTIC, ERR_UNDECLARED_VAR, node->line, node->col, node->data.assign.name);
                }
                bool found_const = false;
                hashmap_get(a->const_vars, node->data.assign.name, &found_const);
                if (found_const) {
                        quil_error_at(STAGE_SEMANTIC, ERR_REASSIGN_CONST, node->line, node->col, node->data.assign.name);
                }
                // element type for arr[i] = v (arrays are homogeneous, so var type == elem type)
                node->resolved_type = strdup(type);
                if (node->data.assign.index) {
                        sem_analyze_node(a, node->data.assign.index);
                }
                sem_analyze_node(a, node->data.assign.value);
                break;
        }

        // ---- Statements (control flow) ----
        case NODE_IF_STAT:
                sem_analyze_node(a, node->data.if_stat.condition);
                sem_analyze_node(a, node->data.if_stat.then_block);
                if (node->data.if_stat.else_block) {
                        sem_analyze_node(a, node->data.if_stat.else_block);
                }
                break;
        case NODE_WHILE:
                sem_analyze_node(a, node->data.while_loop.condition);
                sem_analyze_node(a, node->data.while_loop.body);
                break;
        case NODE_FOR:
                sem_push_scope(a);
                if (node->data.for_loop.init) {
                        sem_analyze_node(a, node->data.for_loop.init);
                }
                if (node->data.for_loop.condition) {
                        sem_analyze_node(a, node->data.for_loop.condition);
                }
                if (node->data.for_loop.increment) {
                        sem_analyze_node(a, node->data.for_loop.increment);
                }
                sem_analyze_node(a, node->data.for_loop.body);
                sem_pop_scope(a);
                break;
        case NODE_RETURN:
                if (node->data.returns.expression) {
                        sem_analyze_node(a, node->data.returns.expression);
                }
                break;
        case NODE_BREAK: break;
        case NODE_CONTINUE: break;

        // ---- Functions ----
        case NODE_FUNC_DEF: {
                funcSig *fs = malloc(sizeof(funcSig));
                // resolve struct return types to qualified names (primitives pass through)
                if (!node->data.func_def.return_type || !strcmp(node->data.func_def.return_type, "void")) {
                        fs->return_type = strdup("void");
                } else {
                        char *rrt = sem_resolve_type(a, node->data.func_def.return_type);
                        if (!rrt) {
                                free(fs);
                                quil_error_at(STAGE_SEMANTIC, ERR_UNDECLARED_TYPE, node->line, node->col, node->data.func_def.return_type);
                        }
                        fs->return_type = sem_fulltype(rrt, NULL, NULL);
                        free(rrt);
                }
                fs->param_count = node->data.func_def.param_count;
                fs->param_types = NULL;
                fs->is_public = node->data.func_def.is_public;
                fs->is_extern = node->data.func_def.is_extern;
                // add functions parameter types if there are
                if (fs->param_count > 0) {
                        fs->param_types = malloc(sizeof(char *) * fs->param_count);
                        for (int i = 0; i < (int)fs->param_count; i++) {
                                ASTnode *p = node->data.func_def.params[i];
                                char *prt = sem_resolve_type(a, p->data.var_decl.type_name);
                                if (!prt) {
                                        for (int j = 0; j < i; j++) free(fs->param_types[j]);
                                        free(fs->param_types);
                                        free(fs->return_type);
                                        free(fs);
                                        quil_error_at(STAGE_SEMANTIC, ERR_UNDECLARED_TYPE, p->line, p->col, p->data.var_decl.type_name);
                                }
                                // sem_fulltype returns a freshly-allocated string
                                fs->param_types[i] = sem_fulltype(prt, p->data.var_decl.modifiers, NULL);
                                free(prt);
                        }
                }

                // mangle with current namespace: std::hi if inside `scope std`
                char *qname;
                if (a->cur_ns) {
                        size_t len = strlen(a->cur_ns) + 2 + strlen(node->data.func_def.name) + 1;
                        qname = malloc(len);
                        snprintf(qname, len, "%s::%s", a->cur_ns, node->data.func_def.name);
                } else {
                        qname = strdup(node->data.func_def.name);
                }
                // register in global function name. note: key = qualified name
                if (!hashmap_insert(a->functions, qname, fs)) {
                        // duplicate function name
                        funcSig_free(fs);
                        free(qname);
                        quil_error_at(STAGE_SEMANTIC, ERR_DUPLICATED_FUNC, node->line, node->col, node->data.func_def.name);
                }

                // extern prototype has no body to analyze
                if (node->data.func_def.is_extern) {
                        break;
                }

                // make params visible inside the function, in the function's OWN scope
                sem_push_scope(a);
                for (size_t i = 0; i < fs->param_count; i++) {
                        ASTnode *p = node->data.func_def.params[i];
                        char *pt = sem_fulltype(p->data.var_decl.type_name, p->data.var_decl.modifiers, NULL);
                        sem_declare(a, p->data.var_decl.name, pt, p->line, p->col);
                        free(pt); // sem_fulltype returns a fresh string; sem_declare strdup'd it
                }
                // body block pushes/pops its own scope INSIDE this function scope
                sem_analyze_node(a, node->data.func_def.body);
                sem_pop_scope(a);

                break;
        }
        case NODE_FUNC_CALL: {
                for (int i = 0; i < node->data.func_call.arg_count; i++) {
                        sem_analyze_node(a, node->data.func_call.args[i]);
                }
                bool found = false;
                funcSig *fs = hashmap_get(a->functions, node->data.func_call.name, &found);
                if (found) {
                        node->resolved_type = fs->return_type ? strdup(fs->return_type) : NULL;
                        if (node->data.func_call.arg_count != (int)fs->param_count) {
                                char message[128];
                                snprintf(message, sizeof message, "function '%s' expects %d argument(s), got %d",
                                         node->data.func_call.name,
                                         (int)fs->param_count,
                                         (int)node->data.func_call.arg_count);
                                quil_error_at(STAGE_SEMANTIC, ERR_ARG_COUNT, node->line, node->col, message);
                                // optional later: compare each arg->resolved_type to s->param_types[i]
                        }
                } else {
                        quil_error_at(STAGE_SEMANTIC, ERR_UNDECLARED_FUNC, node->line, node->col, node->data.func_call.name);
                }
        }

        // ---- Directives & other ----
        case NODE_IMPORT: break;
        case NODE_DIRECTIVE: break;

        // ---- Namespace & qualified ----
        case NODE_NAMESPACE: {
                char *old = a->cur_ns;
                char *next;
                if (old) {
                        size_t len = strlen(old) + 2 + strlen(node->data.namespace_decl.name) + 1;
                        next = malloc(len);
                        snprintf(next, len, "%s::%s", old, node->data.namespace_decl.name);
                } else {
                        next = strdup(node->data.namespace_decl.name);
                }
                a->cur_ns = next;
                sem_analyze_node(a, node->data.namespace_decl.body);
                free(next);
                a->cur_ns = old;
                break;
        }
        case NODE_QUALIFIED:
                // bare qualified path expression (e.g. a::b) - should have been rejected in parser
                // (parser enforces `std::foo()` not `std::foo`), so this is unreachable for calls
                // codegen/sema will resolve qualified calls via NODE_FUNC_CALL name "a::b"
                break;

        // ---- Struct definition ----
        case NODE_STRUCT_DEF: {
                // qualified name like functions: scope std { struct Point } -> std::Point
                char *qname;
                if (a->cur_ns) {
                        size_t len = strlen(a->cur_ns) + 2 + strlen(node->data.struct_def.name) + 1;
                        qname = malloc(len);
                        snprintf(qname, len, "%s::%s", a->cur_ns, node->data.struct_def.name);
                } else {
                        qname = strdup(node->data.struct_def.name);
                }
                StructDef *sd = calloc(1, sizeof(StructDef));
                if (!hashmap_insert(a->types, qname, sd)) {
                        structdef_free(sd);
                        free(qname);
                        quil_error_at(STAGE_SEMANTIC, ERR_DUPLICATED_TYPE, node->line, node->col, node->data.struct_def.name);
                }
                // layout fields with feather addm() padding: align b=1 h=2 w/s=4 l/d=8
                int off = 0, maxal = 0, n = node->data.struct_def.field_count;
                sd->field_names = malloc(sizeof(char *) * (size_t)(n > 0 ? n : 1));
                sd->field_types = malloc(sizeof(char *) * (size_t)(n > 0 ? n : 1));
                sd->field_offsets = malloc(sizeof(int) * (size_t)(n > 0 ? n : 1));
                for (int i = 0; i < n; i++) {
                        ASTnode *f = node->data.struct_def.fields[i];
                        const char *fname = f->data.var_decl.name;
                        for (int j = 0; j < i; j++) {
                                if (!strcmp(sd->field_names[j], fname)) {
                                        quil_error_at(STAGE_SEMANTIC, ERR_REDECLARED_VAR, f->line, f->col, fname);
                                }
                        }
                        char *ft = sem_resolve_type(a, f->data.var_decl.type_name);
                        if (!ft) {
                                quil_error_at(STAGE_SEMANTIC, ERR_UNDECLARED_TYPE, f->line, f->col, f->data.var_decl.type_name);
                        }
                        int al = sem_align_of(a, ft);
                        int sz = sem_size_of(a, ft);
                        if (f->data.var_decl.is_array) {
                                sz *= f->data.var_decl.array_size; // arrays: N contiguous elems, elem align
                        }
                        if (al > maxal) maxal = al;
                        int am = (1 << al) - 1;
                        off = ((off + am) & ~am);
                        sd->field_names[i] = strdup(fname);
                        sd->field_types[i] = ft; // already malloc'd
                        sd->field_offsets[i] = off;
                        off += sz;
                }
                sd->field_count = n;
                sd->size = ((off + (1 << maxal) - 1) & ~((1 << maxal) - 1));
                sd->align = maxal;
                break;
        }

        default:
                break;
        }
}

// --- MAIN ---
void semantic_analyze(ASTnode *program) {
        SemAnalyzer a = {0};
        a.functions = hashmap_create(hm_hash_str, hm_eq_str, free, funcSig_free);
        a.types = hashmap_create(hm_hash_str, hm_eq_str, free, structdef_free);
        a.const_vars = hashmap_create(hm_hash_str, hm_eq_str, free, NULL);
        sem_analyze_node(&a, program);

        // check for fn main() - must be public fn main()
        bool found = false;
        funcSig *mainsig = hashmap_get(a.functions, "main", &found);
        if (!found) {
                quil_error_at(STAGE_SEMANTIC, ERR_NO_MAIN, program->line, program->col, NULL);
        }
        if (!mainsig->is_public) {
                quil_error_at(STAGE_SEMANTIC, ERR_MAIN_NOT_PUBLIC, program->line, program->col, NULL);
        }

        hashmap_free(a.functions);
        hashmap_free(a.types);
        hashmap_free(a.const_vars);
        free(a.frames);
}

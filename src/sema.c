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
#include "../include/methods.h"
#include <stddef.h>
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
                        // file scope may only hold declarations/directives/namespaces;
                        // executables must live inside 'fn main()'
                        if (stmt->type != NODE_FUNC_DEF && stmt->type != NODE_VAR_DECL &&
                            stmt->type != NODE_IMPORT && stmt->type != NODE_DIRECTIVE &&
                            stmt->type != NODE_NAMESPACE) {
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

        // ---- Literals (nothing to check) ----
        case NODE_INT_LITERAL: break;
        case NODE_FLOAT_LITERAL: break;
        case NODE_STRING_LITERAL: break;
        case NODE_BOOL_LITERAL: break;
        case NODE_CHAR_LITERAL: break;
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
                if (node->data.member_access.arg_count > 0) {
                        // method call: validate the method against the shared table
                        if (!obj_type || !method_lookup(obj_type, node->data.member_access.member)) {
                                quil_error_at(STAGE_SEMANTIC, ERR_UNKNOWN, node->line, node->col, node->data.member_access.member);
                        }
                }
                for (int i = 0; i < node->data.member_access.arg_count; i++) {
                        sem_analyze_node(a, node->data.member_access.args[i]);
                }
                // scalar for now (method calls return void; property reads are numeric)
                node->resolved_type = strdup("int32");
                break;
        }

        // ---- Expressions (binary / unary / ternary) ----
        case NODE_BINARY_EXPRESSION:
                sem_analyze_node(a, node->data.binary_expression.left);
                sem_analyze_node(a, node->data.binary_expression.right);
                break;
        case NODE_UNARY_EXPRESSION:
                sem_analyze_node(a, node->data.unary_expression.left);
                break;
        case NODE_TERNARY_EXPRESSION:
                sem_analyze_node(a, node->data.ternary_expression.condition);
                sem_analyze_node(a, node->data.ternary_expression.then_expr);
                sem_analyze_node(a, node->data.ternary_expression.else_expr);
                break;

        // ---- Declarations & assignment ----
        case NODE_VAR_DECL: {
                char *type = sem_fulltype(node->data.var_decl.type_name, node->data.var_decl.modifiers, NULL);
                sem_declare(a, node->data.var_decl.name, type, node->line, node->col);
                free(type); // sem_declare strdup'd it so we can free this copy
                if (node->data.var_decl.value) {
                        sem_analyze_node(a, node->data.var_decl.value);
                }
                break;
        }
        case NODE_ASSIGN: {
                const char *type = sem_resolve(a, node->data.assign.name);
                if (!type) {
                        quil_error_at(STAGE_SEMANTIC, ERR_UNDECLARED_VAR, node->line, node->col, node->data.assign.name);
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
                fs->return_type = node->data.func_def.return_type ? strdup(node->data.func_def.return_type) : strdup("void");
                fs->param_count = node->data.func_def.param_count;
                fs->param_types = NULL;
                fs->is_public = node->data.func_def.is_public;
                fs->is_extern = node->data.func_def.is_extern;
                // add functions parameter types if there are
                if (fs->param_count > 0) {
                        fs->param_types = malloc(sizeof(char *) * fs->param_count);
                        for (int i = 0; i < (int)fs->param_count; i++) {
                                ASTnode *p = node->data.func_def.params[i];
                                // sem_fulltype returns a freshly-allocated string
                                fs->param_types[i] = sem_fulltype(p->data.var_decl.type_name, p->data.var_decl.modifiers, NULL);
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

        default:
                break;
        }
}

// --- MAIN ---
void semantic_analyze(ASTnode *program) {
        SemAnalyzer a = {0};
        a.functions = hashmap_create(hm_hash_str, hm_eq_str, free, funcSig_free);
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
        free(a.frames);
}

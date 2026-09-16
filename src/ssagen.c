/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

/* .ssa code generator */

#include "../include/ssagen.h"
#include "../feather/config.h"
#include "../include/error.h"
#include <complex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// would have been defined feather/main.c
Target T;
extern Target T_amd64_sysv;
extern Target T_amd64_apple;
extern Target T_amd64_win;
extern Target T_arm64;
extern Target T_arm64_apple;
extern Target T_rv64;
int optlevel = 0; // no optimization by default
char debug['Z' + 1];

typedef struct {
        char **field_names;
        int *field_offsets;
        int field_count;
        int size;
        int align; // log2 alignment
} SsaStruct;

typedef struct {
        ILBuilder *ilb;
        HashMap *slots;        /* name -> Ref* (alloc addr, Kl) */
        HashMap *externs;      /* mangled name -> present (extern prototypes) */
        HashMap *struct_types; /* qualified name -> SsaStruct* (first-pass layouts) */
        IlModule *mod;
        char *cur_ns;         /* current namespace("scope" keyword) */
        Blk *break_target;    // current loop merge block
        Blk *continue_target; // current loop cond block
} Ssagen;

/* --- Prototypes --- */
static int quil_to_cls(const char *t);
static int elem_size_of(const char *t);
static void emit_store_elem(Ssagen *s, const char *t, Ref v, Ref addr);
static Ref emit_load_elem(Ssagen *s, const char *t, Ref addr);
static Ref emit_obj_addr(Ssagen *s, ASTnode *obj);
static int ssa_align_of(Ssagen *s, const char *t);
static int ssa_type_size(Ssagen *s, const char *t);
static Ref emit_expr(Ssagen *s, ASTnode *n);
static void emit_stmt(Ssagen *s, ASTnode *n);
static char *mangle(const char *qname);
static void emit_func(Ssagen *s, ASTnode *fndef);

/* --- HELPER --- */
// enum { Kx=-1, Kw, Kl, Ks, Kd };
static int quil_to_cls(const char *t) {
        if (!t) return Kw;
        if (!strcmp(t, "int8") || !strcmp(t, "int16") || !strcmp(t, "int32") ||
            !strcmp(t, "bool") || !strcmp(t, "uint8") || !strcmp(t, "uint16") ||
            !strcmp(t, "uint32")) {
                return Kw;
        }
        if (!strcmp(t, "int64") || !strcmp(t, "uint64") || !strcmp(t, "string") ||
            !strcmp(t, "char*") || !strcmp(t, "char *")) {
                return Kl; // pointers are l (sema spells it "char *" with a space)
        }
        if (!strcmp(t, "char")) return Kw; // char literal is w (byte promoted)
        if (!strcmp(t, "float32")) return Ks;
        if (!strcmp(t, "float64")) return Kd;
        return Kw;
}
// byte size of one element for contiguous array layout (Kw values still live in 32b regs)
static int elem_size_of(const char *t) {
        if (!t) return 4;
        if (!strcmp(t, "int8") || !strcmp(t, "uint8") || !strcmp(t, "char") || !strcmp(t, "bool")) return 1;
        if (!strcmp(t, "int16") || !strcmp(t, "uint16")) return 2;
        if (!strcmp(t, "int32") || !strcmp(t, "uint32") || !strcmp(t, "float32")) return 4;
        if (!strcmp(t, "int64") || !strcmp(t, "uint64") || !strcmp(t, "float64") || !strcmp(t, "string")) return 8;
        if (t[strlen(t) - 1] == '*') return 8; // all pointers are l
        return 4;
}
// store with memory width matching the element type (storeb/h for 1/2-byte elems)
static void emit_store_elem(Ssagen *s, const char *t, Ref v, Ref addr) {
        if (!t) t = "int32";
        if (!strcmp(t, "int8") || !strcmp(t, "uint8") || !strcmp(t, "char") || !strcmp(t, "bool")) {
                il_create_store_b(s->ilb, v, addr);
                return;
        }
        if (!strcmp(t, "int16") || !strcmp(t, "uint16")) {
                il_create_store_h(s->ilb, v, addr);
                return;
        }
        il_create_store(s->ilb, quil_to_cls(t), v, addr);
}
// load with memory width matching the element type (unsigned uses zero-extend)
static Ref emit_load_elem(Ssagen *s, const char *t, Ref addr) {
        if (!t) t = "int32";
        if (!strcmp(t, "int8") || !strcmp(t, "char")) return il_create_load_sb(s->ilb, addr);
        if (!strcmp(t, "uint8") || !strcmp(t, "bool")) return il_create_load_ub(s->ilb, addr);
        if (!strcmp(t, "int16")) return il_create_load_sh(s->ilb, addr);
        if (!strcmp(t, "uint16")) return il_create_load_uh(s->ilb, addr);
        int cls = quil_to_cls(t);
        if (cls == Kl) return il_create_load_l(s->ilb, addr);
        if (cls == Ks) return il_create_load_s(s->ilb, addr);
        if (cls == Kd) return il_create_load_d(s->ilb, addr);
        return il_create_load_w(s->ilb, addr);
}
static void ssa_struct_free(void *p) {
        SsaStruct *sd = (SsaStruct *)p;
        if (!sd) return;
        for (int i = 0; i < sd->field_count; i++) free(sd->field_names[i]);
        free(sd->field_names);
        free(sd->field_offsets);
        free(sd);
}

// qualified name of a struct def (malloc'd, caller frees or hands to table)
static char *ssa_struct_qname(Ssagen *s, const char *name) {
        if (!s->cur_ns) return strdup(name);
        size_t len = strlen(s->cur_ns) + 2 + strlen(name) + 1;
        char *q = malloc(len);
        snprintf(q, len, "%s::%s", s->cur_ns, name);
        return q;
}

// log2 alignment for layout (b=0 h=1 w/s=2 l/d=3, structs from table)
static int ssa_align_of(Ssagen *s, const char *t) {
        if (!strcmp(t, "int8") || !strcmp(t, "uint8") || !strcmp(t, "char") || !strcmp(t, "bool")) return 0;
        if (!strcmp(t, "int16") || !strcmp(t, "uint16")) return 1;
        if (!strcmp(t, "int32") || !strcmp(t, "uint32") || !strcmp(t, "float32")) return 2;
        if (!strcmp(t, "int64") || !strcmp(t, "uint64") || !strcmp(t, "float64") || !strcmp(t, "string")) return 3;
        if (t[strlen(t) - 1] == '*') return 3;
        bool found = false;
        SsaStruct *sd = hashmap_get(s->struct_types, t, &found);
        if (!found && s->cur_ns) {
                size_t len = strlen(s->cur_ns) + 2 + strlen(t) + 1;
                char *q = malloc(len);
                snprintf(q, len, "%s::%s", s->cur_ns, t);
                sd = hashmap_get(s->struct_types, q, &found);
                free(q);
        }
        if (found) return sd->align;
        return 2;
}

// true when t names a registered struct (raw or cur_ns-qualified)
static bool ssa_is_struct(Ssagen *s, const char *t) {
        if (!t) return false;
        bool found = false;
        hashmap_get(s->struct_types, t, &found);
        if (!found && s->cur_ns) {
                size_t len = strlen(s->cur_ns) + 2 + strlen(t) + 1;
                char *q = malloc(len);
                snprintf(q, len, "%s::%s", s->cur_ns, t);
                hashmap_get(s->struct_types, q, &found);
                free(q);
        }
        return found;
}

// total byte size of a type (arrays handled by caller via elem_size_of * count)
static int ssa_type_size(Ssagen *s, const char *t) {
        // structs first: a struct with max align w (e.g. Point) must not hit the primitive fast path
        bool found = false;
        SsaStruct *sd = hashmap_get(s->struct_types, t, &found);
        if (!found && s->cur_ns) {
                size_t len = strlen(s->cur_ns) + 2 + strlen(t) + 1;
                char *q = malloc(len);
                snprintf(q, len, "%s::%s", s->cur_ns, t);
                sd = hashmap_get(s->struct_types, q, &found);
                free(q);
        }
        if (found) return sd->size;
        int al = ssa_align_of(s, t);
        if (al == 0) return 1;
        if (al == 1) return 2;
        if (al == 2) return 4;
        return 8; // Kl/Kd classes
}

// first pass: register struct layouts (same addm padding as sema)
static void collect_struct(Ssagen *s, ASTnode *def) {
        SsaStruct *sd = malloc(sizeof(SsaStruct));
        int n = def->data.struct_def.field_count;
        sd->field_names = malloc(sizeof(char *) * (size_t)(n > 0 ? n : 1));
        sd->field_offsets = malloc(sizeof(int) * (size_t)(n > 0 ? n : 1));
        sd->field_count = n;
        int off = 0, maxal = 0;
        for (int i = 0; i < n; i++) {
                ASTnode *f = def->data.struct_def.fields[i];
                int al = ssa_align_of(s, f->data.var_decl.type_name);
                int sz = ssa_type_size(s, f->data.var_decl.type_name);
                if (f->data.var_decl.is_array) sz *= f->data.var_decl.array_size;
                if (al > maxal) maxal = al;
                int am = (1 << al) - 1;
                off = ((off + am) & ~am);
                sd->field_names[i] = strdup(f->data.var_decl.name);
                sd->field_offsets[i] = off;
                off += sz;
        }
        sd->size = ((off + (1 << maxal) - 1) & ~((1 << maxal) - 1));
        sd->align = maxal;
        hashmap_put(s->struct_types, ssa_struct_qname(s, def->data.struct_def.name), sd);
}

// address of an lvalue object: identifier -> slot, arr[i] -> base + i*size,
// member -> parent base + field offset
static Ref emit_obj_addr(Ssagen *s, ASTnode *obj) {
        if (obj->type == NODE_IDENTIFIER) {
                bool found;
                Ref *slotp = hashmap_get(s->slots, obj->data.identifier.name, &found);
                if (!found) quil_error(STAGE_CODEGEN, ERR_UNDECLARED_VAR, obj->data.identifier.name);
                return *slotp;
        }
        if (obj->type == NODE_ARRAY_ACCESS) {
                bool found;
                Ref *sp = hashmap_get(s->slots, obj->data.array_access.name, &found);
                if (!found) quil_error(STAGE_CODEGEN, ERR_UNDECLARED_VAR, obj->data.array_access.name);
                Ref base = *sp;
                Ref idx = emit_expr(s, obj->data.array_access.index);
                int idx_cls = quil_to_cls(obj->data.array_access.index->resolved_type);
                int sz = ssa_type_size(s, obj->resolved_type ? obj->resolved_type : "int32");
                Ref off = (idx_cls == Kl) ? il_create_mul_l(s->ilb, idx, il_const_int_l(s->ilb, sz))
                                          : il_create_mul_w(s->ilb, idx, il_const_int_w(s->ilb, sz));
                Ref off_l = (idx_cls == Kl) ? off : il_create_extsw_l(s->ilb, off);
                return il_create_add_l(s->ilb, base, off_l);
        }
        if (obj->type == NODE_MEMBER_ACCESS) {
                Ref base = emit_obj_addr(s, obj->data.member_access.object);
                int off = obj->data.member_access.field_offset;
                if (off < 0) quil_error(STAGE_CODEGEN, ERR_UNKNOWN, obj->data.member_access.member);
                if (off == 0) return base;
                return il_create_add_l(s->ilb, base, il_const_int_l(s->ilb, off));
        }
        quil_error(STAGE_CODEGEN, ERR_UNKNOWN, node_type_name(obj->type));
}
static Ref emit_expr(Ssagen *s, ASTnode *n) {
        switch (n->type) {
        /* Literals */
        case NODE_INT_LITERAL: {
                // int8/int16/bool are still Kw (32b value, stored via Ostoreb/Ostoreh)
                int cls = quil_to_cls(n->resolved_type);
                if (cls == Kl) return il_const_int_l(s->ilb, (int64_t)n->data.int_literal.value);
                return il_const_int_w(s->ilb, n->data.int_literal.value);
        }
        case NODE_FLOAT_LITERAL: {
                int cls = quil_to_cls(n->resolved_type);
                if (cls == Kd) return il_const_float_d(s->ilb, n->data.float_literal.value);
                return il_const_float_s(s->ilb, (float)n->data.float_literal.value);
        }
        case NODE_BOOL_LITERAL:
                return il_const_int_w(s->ilb, n->data.bool_literal.value ? 1 : 0);
        case NODE_CHAR_LITERAL:
                return il_const_int_w(s->ilb, (int)n->data.char_literal.value);
        case NODE_STRING_LITERAL: {
                // data .str(str_id) = {b str b 0}
                static int str_id = 0;
                char name[32];
                // generate distinct data name each time
                snprintf(name, sizeof(name), ".str%d", str_id++);
                Lnk lnk = {0};
                IlData *data = il_data_begin(name, &lnk);
                il_data_add_str(data, DB, n->data.string_literal.value);
                il_data_add_b(data, 0);
                il_data_end(data);
                il_module_add_data(s->mod, data);
                // return address of data
                return il_global_sym(s->ilb, name);
        }
        case NODE_IDENTIFIER: {
                bool found;
                Ref *slotp = hashmap_get(s->slots, n->data.identifier.name, &found);
                if (!found) quil_error(STAGE_CODEGEN, ERR_UNDECLARED_VAR, n->data.identifier.name);
                Ref slot = *slotp;
                int cls = quil_to_cls(n->resolved_type);
                if (cls == Kl) return il_create_load_l(s->ilb, slot);
                if (cls == Ks) return il_create_load_s(s->ilb, slot);
                if (cls == Kd) return il_create_load_d(s->ilb, slot);
                return il_create_load_w(s->ilb, slot); // Kw bool char int8/16/32
        }
        case NODE_ARRAY_ACCESS: { // arr[i] -> *(base + i * size)
                bool found;
                Ref *sp = hashmap_get(s->slots, n->data.array_access.name, &found);
                if (!found) quil_error(STAGE_CODEGEN, ERR_UNDECLARED_VAR, n->data.array_access.name);
                Ref base = *sp;

                Ref idx = emit_expr(s, n->data.array_access.index);
                int idx_cls = quil_to_cls(n->data.array_access.index->resolved_type);
                const char *et = n->resolved_type ? n->resolved_type : "int32";
                int sz = elem_size_of(et);
                Ref off = (idx_cls == Kl) ? il_create_mul_l(s->ilb, idx, il_const_int_l(s->ilb, sz))
                                          : il_create_mul_w(s->ilb, idx, il_const_int_w(s->ilb, sz));
                Ref off_l = (idx_cls == Kl) ? off : il_create_extsw_l(s->ilb, off);
                Ref addr = il_create_add_l(s->ilb, base, off_l); // Kl
                return emit_load_elem(s, et, addr);
        }
        /* binray unary ternary expressions */
        case NODE_BINARY_EXPRESSION: {
                Ref l = emit_expr(s, n->data.binary_expression.left);
                Ref r = emit_expr(s, n->data.binary_expression.right);
                tokenType op = n->data.binary_expression.op;
                int cls = quil_to_cls(n->resolved_type);
                if (cls == Kd) {
                        switch (op) {
                        case TOKEN_PLUS: return il_create_add_d(s->ilb, l, r);
                        case TOKEN_MINUS: return il_create_sub_d(s->ilb, l, r);
                        case TOKEN_STAR: return il_create_mul_d(s->ilb, l, r);
                        case TOKEN_FSLASH: return il_create_div_d(s->ilb, l, r);
                        default: break;
                        }
                } else if (cls == Ks) {
                        switch (op) {
                        case TOKEN_PLUS: return il_create_add_s(s->ilb, l, r);
                        case TOKEN_MINUS: return il_create_sub_s(s->ilb, l, r);
                        case TOKEN_STAR: return il_create_mul_s(s->ilb, l, r);
                        case TOKEN_FSLASH: return il_create_div_s(s->ilb, l, r);
                        default: break;
                        }
                } else if (cls == Kl) {
                        switch (op) {
                        case TOKEN_PLUS: return il_create_add_l(s->ilb, l, r);
                        case TOKEN_MINUS: return il_create_sub_l(s->ilb, l, r);
                        case TOKEN_STAR: return il_create_mul_l(s->ilb, l, r);
                        case TOKEN_FSLASH: return il_create_div_l(s->ilb, l, r);
                        case TOKEN_PERCENT: return il_create_rem_l(s->ilb, l, r);
                        case TOKEN_AND: return il_create_and_l(s->ilb, l, r);
                        case TOKEN_PIPE: return il_create_or_l(s->ilb, l, r);
                        case TOKEN_CARET: return il_create_xor_l(s->ilb, l, r);
                        default: break;
                        }
                        // comparisons
                        if (op == TOKEN_EEQUAL) return il_create_icmp_eq_l(s->ilb, l, r);
                        if (op == TOKEN_NEQUAL) return il_create_icmp_ne_l(s->ilb, l, r);
                        if (op == TOKEN_LABRACKET) return il_create_icmp_slt_l(s->ilb, l, r);
                        if (op == TOKEN_RABRACKET) return il_create_icmp_sgt_l(s->ilb, l, r);
                        if (op == TOKEN_LEQUAL) return il_create_icmp_sle_l(s->ilb, l, r);
                        if (op == TOKEN_GEQUAL) return il_create_icmp_sge_l(s->ilb, l, r);
                } else /* Kw */ {
                        switch (op) {
                        case TOKEN_PLUS: return il_create_add_w(s->ilb, l, r);
                        case TOKEN_MINUS: return il_create_sub_w(s->ilb, l, r);
                        case TOKEN_STAR: return il_create_mul_w(s->ilb, l, r);
                        case TOKEN_FSLASH: return il_create_div_w(s->ilb, l, r);
                        case TOKEN_PERCENT: return il_create_rem_w(s->ilb, l, r);
                        case TOKEN_AND: return il_create_and_w(s->ilb, l, r);
                        case TOKEN_PIPE: return il_create_or_w(s->ilb, l, r);
                        case TOKEN_CARET: return il_create_xor_w(s->ilb, l, r);
                        default: break;
                        }
                        if (op == TOKEN_EEQUAL) return il_create_icmp_eq_w(s->ilb, l, r);
                        if (op == TOKEN_NEQUAL) return il_create_icmp_ne_w(s->ilb, l, r);
                        if (op == TOKEN_LABRACKET) return il_create_icmp_slt_w(s->ilb, l, r);
                        if (op == TOKEN_RABRACKET) return il_create_icmp_sgt_w(s->ilb, l, r);
                        if (op == TOKEN_LEQUAL) return il_create_icmp_sle_w(s->ilb, l, r);
                        if (op == TOKEN_GEQUAL) return il_create_icmp_sge_w(s->ilb, l, r);
                        if (op == TOKEN_OR) return il_create_or_w(s->ilb, l, r);
                        if (op == TOKEN_AND) return il_create_and_w(s->ilb, l, r);
                }
                return l;
        }
        case NODE_UNARY_EXPRESSION: {
                Ref v = emit_expr(s, n->data.unary_expression.left);
                tokenType op = n->data.unary_expression.op;
                int cls = quil_to_cls(n->resolved_type);
                if (op == TOKEN_MINUS) {
                        if (cls == Kd) return il_create_neg_d(s->ilb, v);
                        if (cls == Ks) return il_create_neg_s(s->ilb, v);
                        if (cls == Kl) return il_create_neg_l(s->ilb, v);
                        return il_create_neg_w(s->ilb, v);
                }
                if (op == TOKEN_EXCLAMATION) {
                        // !a  ->  a == 0  (Kw bool 0/1)
                        return il_create_icmp_eq_w(s->ilb, v, il_const_zero(s->ilb));
                }
                quil_error(STAGE_CODEGEN, ERR_UNKNOWN, lexer_token_type_to_string(op));
        }
        case NODE_TERNARY_EXPRESSION: {
                Ref cond = emit_expr(s, n->data.ternary_expression.condition);
                Blk *then_blk = il_create_block(s->ilb, "tern.then");
                Blk *else_blk = il_create_block(s->ilb, "tern.else");
                Blk *merge = il_create_block(s->ilb, "tern.merge");
                il_create_cond_br(s->ilb, cond, then_blk, else_blk); // cur terminated

                // then
                il_set_insert_point(s->ilb, then_blk);
                Ref tv = emit_expr(s, n->data.ternary_expression.then_expr);
                il_create_br(s->ilb, merge);
                // else
                il_set_insert_point(s->ilb, else_blk);
                Ref ev = emit_expr(s, n->data.ternary_expression.else_expr);
                il_create_br(s->ilb, merge);

                // merge phi
                il_set_insert_point(s->ilb, merge);
                Blk *preds[] = {then_blk, else_blk};
                Ref vals[] = {tv, ev};
                int cls = quil_to_cls(n->resolved_type);
                if (cls == Kl) return il_create_phi_l(s->ilb, preds, vals, 2);
                if (cls == Kd) return il_create_phi_d(s->ilb, preds, vals, 2);
                if (cls == Ks) return il_create_phi_s(s->ilb, preds, vals, 2);
                return il_create_phi_w(s->ilb, preds, vals, 2);
        }
        case NODE_MEMBER_ACCESS: { // obj.field read (struct only; methods rejected in sema)
                if (n->data.member_access.arg_count > 0) {
                        quil_error(STAGE_CODEGEN, ERR_UNKNOWN, n->data.member_access.member);
                }
                Ref base = emit_obj_addr(s, n->data.member_access.object);
                int off = n->data.member_access.field_offset;
                if (off < 0) quil_error(STAGE_CODEGEN, ERR_UNKNOWN, n->data.member_access.member);
                Ref addr = (off == 0) ? base : il_create_add_l(s->ilb, base, il_const_int_l(s->ilb, off));
                return emit_load_elem(s, n->resolved_type, addr);
        }
        case NODE_FUNC_CALL: {
                int nargs = n->data.func_call.arg_count;
                Ref *args = NULL;
                if (nargs > 0) {
                        args = emalloc(sizeof(Ref) * (size_t)nargs);
                        for (int i = 0; i < nargs; i++) {
                                ASTnode *a = n->data.func_call.args[i];
                                // struct args pass by address (no struct-by-value yet)
                                if (a->resolved_type && ssa_is_struct(s, a->resolved_type)) {
                                        args[i] = emit_obj_addr(s, a);
                                } else {
                                        args[i] = emit_expr(s, a);
                                }
                        }
                }
                char *mangled = mangle(n->data.func_call.name);
                bool is_ext = false;
                if (s->externs) {
                        bool found = false;
                        hashmap_get(s->externs, mangled, &found);
                        is_ext = found;
                }
                Ref callee = is_ext ? il_extern_sym(s->ilb, mangled) : il_global_sym(s->ilb, mangled);
                const char *rt = n->resolved_type;
                bool is_void = (!rt || !strcmp(rt, "void"));
                if (is_void) {
                        for (int i = 0; i < nargs; i++) il_call_arg(s->ilb, args[i]);
                        Ins ci = {.op = Ocall, .cls = Kw, .to = R, .arg = {callee, R}};
                        addins(&s->ilb->cur->ins, &s->ilb->cur->nins, &ci);
                        return CON_Z;
                }
                int cls = quil_to_cls(rt);
                if (cls == Kl) return il_create_call_l(s->ilb, callee, args, nargs);
                if (cls == Ks) return il_create_call_s(s->ilb, callee, args, nargs);
                if (cls == Kd) return il_create_call_d(s->ilb, callee, args, nargs);
                return il_create_call_w(s->ilb, callee, args, nargs);
        }
        default:
                quil_error(STAGE_CODEGEN, ERR_UNKNOWN, node_type_name(n->type));
        }
}
static void emit_stmt(Ssagen *s, ASTnode *n) {
        switch (n->type) {
        case NODE_VAR_DECL: {
                const char *t = n->data.var_decl.type_name;
                int cls = quil_to_cls(t);
                // ssa_type_size covers primitives + structs (elem_size_of falls back to 4 for structs)
                int sz = ssa_type_size(s, t);
                if (sz != 1 && sz != 2 && sz != 4 && sz != 8) cls = Kl; // struct value: address-sized slot
                if (n->data.var_decl.is_array) sz *= n->data.var_decl.array_size;

                Ref slot = il_create_alloc4(s->ilb, il_const_int_w(s->ilb, sz)); // always l
                Ref *rp = emalloc(sizeof(Ref));
                *rp = slot;
                hashmap_put(s->slots, strdup(n->data.var_decl.name), rp);
                if (n->data.var_decl.value) {
                        // array init [1,2,3] -> store each element at base + i*elem_size
                        if (n->data.var_decl.value->type == NODE_LIST_LITERAL) {
                                ASTnode *lst = n->data.var_decl.value;
                                int esz = ssa_type_size(s, t);
                                for (int i = 0; i < lst->data.list_literal.count; i++) {
                                        Ref ev = emit_expr(s, lst->data.list_literal.elements[i]);
                                        Ref off = il_create_mul_w(s->ilb, il_const_int_w(s->ilb, i),
                                                                                il_const_int_w(s->ilb, esz));
                                        Ref addr = il_create_add_l(s->ilb, slot, il_create_extsw_l(s->ilb, off));
                                        emit_store_elem(s, t, ev, addr);
                                }
                        } else {
                                Ref v = emit_expr(s, n->data.var_decl.value);
                                emit_store_elem(s, t, v, slot);
                        }
                }
                break;
        }
        case NODE_ASSIGN: {
                if (n->data.assign.is_member) {
                        // obj.field = v
                        Ref v = emit_expr(s, n->data.assign.value);
                        Ref base = emit_obj_addr(s, n->data.assign.obj);
                        int off = n->data.assign.field_offset;
                        if (off < 0) quil_error(STAGE_CODEGEN, ERR_UNKNOWN, n->data.assign.member);
                        Ref addr = (off == 0) ? base : il_create_add_l(s->ilb, base, il_const_int_l(s->ilb, off));
                        emit_store_elem(s, n->resolved_type, v, addr);
                        break;
                }
                bool found;
                Ref *sp = hashmap_get(s->slots, n->data.assign.name, &found);
                if (!found) quil_error(STAGE_CODEGEN, ERR_UNDECLARED_VAR, n->data.assign.name);
                Ref slot = *sp;
                Ref v = emit_expr(s, n->data.assign.value);
                if (n->data.assign.index) {
                        Ref idx = emit_expr(s, n->data.assign.index);
                        int idx_cls = quil_to_cls(n->data.assign.index->resolved_type);
                        const char *et = n->resolved_type ? n->resolved_type : "int32";
                        int elem_size = elem_size_of(et);
                        Ref off;
                        if (idx_cls == Kl) {
                                off = il_create_mul_l(s->ilb, idx, il_const_int_l(s->ilb, elem_size));
                        } else {
                                off = il_create_mul_w(s->ilb, idx, il_const_int_w(s->ilb, elem_size));
                        }
                        Ref off_l = (idx_cls == Kl) ? off : il_create_extsw_l(s->ilb, off);
                        Ref addr = il_create_add_l(s->ilb, slot, off_l);
                        emit_store_elem(s, et, v, addr);
                } else {
                        int cls = quil_to_cls(n->resolved_type ? n->resolved_type : n->data.assign.value->resolved_type);
                        if (!cls) cls = quil_to_cls("int32");
                        il_create_store(s->ilb, cls, v, slot);
                }
                break;
        }
        case NODE_RETURN: {
                if (!n->data.returns.expression) {
                        il_create_ret_void(s->ilb);
                } else {
                        Ref v = emit_expr(s, n->data.returns.expression);
                        int cls = quil_to_cls(n->data.returns.expression->resolved_type);
                        if (cls == Kl) il_create_ret_l(s->ilb, v);
                        else if (cls == Kd) il_create_ret_d(s->ilb, v);
                        else if (cls == Ks) il_create_ret_s(s->ilb, v);
                        else il_create_ret_w(s->ilb, v);
                }
                break;
        }
        case NODE_BLOCK: {
                for (int i = 0; i < n->data.blocks.count; i++) {
                        emit_stmt(s, n->data.blocks.statements[i]);
                }
                break;
        }
        case NODE_IF_STAT: {
                Ref cond = emit_expr(s, n->data.if_stat.condition);
                Blk *then_blk = il_create_block(s->ilb, "if.then");
                Blk *else_blk = n->data.if_stat.else_block ? il_create_block(s->ilb, "if.else") : NULL;
                Blk *merge = il_create_block(s->ilb, "if.merge");
                if (else_blk) {
                        il_create_cond_br(s->ilb, cond, then_blk, else_blk);
                } else {
                        il_create_cond_br(s->ilb, cond, then_blk, merge);
                }

                // then
                il_set_insert_point(s->ilb, then_blk);
                emit_stmt(s, n->data.if_stat.then_block);
                if (s->ilb->cur) il_create_br(s->ilb, merge); // cur may be NULL if then had return

                // else
                if (else_blk) {
                        il_set_insert_point(s->ilb, else_blk);
                        emit_stmt(s, n->data.if_stat.else_block);
                        if (s->ilb->cur) il_create_br(s->ilb, merge);
                }
                il_set_insert_point(s->ilb, merge);
                break;
        }
        case NODE_WHILE: {
                Blk *cond_blk = il_create_block(s->ilb, "while.cond");
                Blk *body_blk = il_create_block(s->ilb, "while.body");
                Blk *merge = il_create_block(s->ilb, "while.merge");
                Blk *old_break = s->break_target;
                Blk *old_cont = s->continue_target;
                s->break_target = merge;
                s->continue_target = cond_blk;

                il_create_br(s->ilb, cond_blk);
                il_set_insert_point(s->ilb, cond_blk);
                Ref cond = emit_expr(s, n->data.while_loop.condition);

                il_create_cond_br(s->ilb, cond, body_blk, merge);
                il_set_insert_point(s->ilb, body_blk);
                emit_stmt(s, n->data.while_loop.body);
                if (s->ilb->cur) il_create_br(s->ilb, cond_blk);
                il_set_insert_point(s->ilb, merge);
                s->break_target = old_break;
                s->continue_target = old_cont;
                break;
        }
        case NODE_FOR: {
                // for(init; cond; inc) { body }  and  for(range) { body }  sugar
                // is_range: for(5) -> init=range_expr(5), cond=NULL, inc=NULL, init!=VAR_DECL src/parser.c:212
                bool is_range = n->data.for_loop.init && !n->data.for_loop.condition && !n->data.for_loop.increment && n->data.for_loop.init->type != NODE_VAR_DECL;
                Blk *old_break = s->break_target;
                Blk *old_cont = s->continue_target;
                if (is_range) {
                        // backend 0 temp: user can't write "0" as ID (TOKEN_INUM include/lexer.h:22), so no collision
                        static int range_id = 0; // unique per for(range) nesting
                        char name[16];
                        snprintf(name, sizeof(name), "%d", range_id++); // "0","1",...
                        // alloc counter slot (Kw 4 bytes) and init to 0
                        Ref ctr_slot = il_create_alloc4(s->ilb, il_const_int_w(s->ilb, 4)); // l addr, Kw slot
                        Ref *rp = emalloc(sizeof(Ref));
                        *rp = ctr_slot;
                        hashmap_put(s->slots, strdup(name), rp);                        // slots name->Ref for load/store
                        il_create_store_w(s->ilb, il_const_int_w(s->ilb, 0), ctr_slot); // ctr = 0

                        // range value (e.g. 5 or var) evaluated once before loop
                        Ref range_val = emit_expr(s, n->data.for_loop.init); // Kw
                        // blocks: cond -> body -> inc -> merge
                        Blk *cond_blk = il_create_block(s->ilb, "for.cond");
                        Blk *body_blk = il_create_block(s->ilb, "for.body");
                        Blk *inc_blk = il_create_block(s->ilb, "for.inc");
                        Blk *merge = il_create_block(s->ilb, "for.merge");
                        s->break_target = merge;
                        s->continue_target = inc_blk;
                        il_create_br(s->ilb, cond_blk); // jump to first condition check

                        // cond block: cur < range ?
                        il_set_insert_point(s->ilb, cond_blk);
                        Ref cur = il_create_load_w(s->ilb, ctr_slot);            // load counter
                        Ref cond = il_create_icmp_slt_w(s->ilb, cur, range_val); // cur < range (Kw bool)
                        il_create_cond_br(s->ilb, cond, body_blk, merge);        // cur terminated

                        // body block: emit user body
                        il_set_insert_point(s->ilb, body_blk);
                        emit_stmt(s, n->data.for_loop.body); // BLOCK include/ast.h:50
                        if (s->ilb->cur) il_create_br(s->ilb, inc_blk);
                        // inc block: increment counter and loop back
                        il_set_insert_point(s->ilb, inc_blk);
                        Ref cur2 = il_create_load_w(s->ilb, ctr_slot);
                        Ref inc = il_create_add_w(s->ilb, cur2, il_const_int_w(s->ilb, 1));
                        il_create_store_w(s->ilb, inc, ctr_slot);
                        il_create_br(s->ilb, cond_blk);
                        il_set_insert_point(s->ilb, merge); // continue after loop
                        s->break_target = old_break;
                        s->continue_target = old_cont;
                } else {
                        // normal for: for(init; cond; inc) { body } src/parser.c:194
                        if (n->data.for_loop.init) emit_stmt(s, n->data.for_loop.init); // VAR_DECL or assign
                        Blk *cond_blk = il_create_block(s->ilb, "for.cond");
                        Blk *body_blk = il_create_block(s->ilb, "for.body");
                        Blk *inc_blk = il_create_block(s->ilb, "for.inc");
                        Blk *merge = il_create_block(s->ilb, "for.merge");
                        Blk *saved_break = s->break_target;
                        Blk *saved_cont = s->continue_target;
                        s->break_target = merge;
                        s->continue_target = inc_blk;
                        il_create_br(s->ilb, cond_blk);

                        // cond block: evaluate condition or unconditional jump
                        il_set_insert_point(s->ilb, cond_blk);
                        if (n->data.for_loop.condition) {
                                Ref cond = emit_expr(s, n->data.for_loop.condition); // Kw bool
                                il_create_cond_br(s->ilb, cond, body_blk, merge);
                        } else {
                                il_create_br(s->ilb, body_blk); // for(;;) infinite
                        }

                        // body
                        il_set_insert_point(s->ilb, body_blk);
                        emit_stmt(s, n->data.for_loop.body);
                        if (s->ilb->cur) il_create_br(s->ilb, inc_blk);
                        // inc block
                        il_set_insert_point(s->ilb, inc_blk);
                        if (n->data.for_loop.increment) {
                                if (n->data.for_loop.increment->type == NODE_ASSIGN) emit_stmt(s, n->data.for_loop.increment);
                                else emit_expr(s, n->data.for_loop.increment);
                        }
                        il_create_br(s->ilb, cond_blk);
                        il_set_insert_point(s->ilb, merge);
                        s->break_target = saved_break;
                        s->continue_target = saved_cont;
                }
                break;
        }
        case NODE_BREAK: {
                if (!s->break_target) quil_error(STAGE_CODEGEN, ERR_UNKNOWN, "break outside loop");
                il_create_br(s->ilb, s->break_target);
                s->ilb->cur = NULL;
                break;
        }
        case NODE_CONTINUE: {
                if (!s->continue_target) quil_error(STAGE_CODEGEN, ERR_UNKNOWN, "continue outside loop");
                il_create_br(s->ilb, s->continue_target);
                s->ilb->cur = NULL;
                break;
        }
        case NODE_FUNC_CALL: {
                // expression-statement foo(); -> emit call, ignore result
                emit_expr(s, n);
                break;
        }
        case NODE_IMPORT:
        case NODE_DIRECTIVE:
                break;
        default:
                break;
        }
}

static char *mangle(const char *qname) {
        // "_" -> "_0", "::" -> "_1" so "std_foo" (std_0foo) != "std::foo" (std_1foo)
        // "a::b_c" -> "a_1b_0c"
        size_t n = strlen(qname);
        char *out = emalloc(n * 2 + 1);
        char *p = out;
        for (size_t i = 0; i < n;) {
                if (qname[i] == '_') {
                        *p++ = '_';
                        *p++ = '0';
                        i++;
                } else if (qname[i] == ':' && i + 1 < n && qname[i + 1] == ':') {
                        *p++ = '_';
                        *p++ = '1';
                        i += 2;
                } else {
                        *p++ = qname[i++];
                }
        }
        *p = '\0';
        return out;
}
static void emit_func(Ssagen *s, ASTnode *fndef) {
        if (fndef->data.func_def.is_extern) {
                return; // if its is extern then its prototype only function will be defined elsewhere
        }
        char *qname = s->cur_ns ? strf(PHeap, "%s::%s", s->cur_ns, fndef->data.func_def.name) : fndef->data.func_def.name;
        char *mangled = mangle(qname);
        qname = mangled;
        Lnk lnk = {.export = fndef->data.func_def.is_public || strcmp(fndef->data.func_def.name, "main") == 0}; // public fn main required
        Fn *fn = il_create_function(qname, Kx, &lnk);
        s->ilb = il_create(fn);
        Blk *entry = il_create_block(s->ilb, "entry");
        il_set_insert_point(s->ilb, entry); // ilbuilder.c:25 cur

        // params must all come first (filapi asserts Opar lead): collect first, then alloc/store
        // struct params arrive by address (caller passes slot addr, see FUNC_CALL)
        int pc = fndef->data.func_def.param_count;
        Ref *prs = NULL;
        if (pc > 0) {
                prs = emalloc(sizeof(Ref) * (size_t)pc);
                for (int i = 0; i < pc; i++) {
                        ASTnode *p = fndef->data.func_def.params[i];
                        bool ps = ssa_is_struct(s, p->data.var_decl.type_name);
                        prs[i] = il_add_param(s->ilb, ps ? Kl : quil_to_cls(p->data.var_decl.type_name));
                }
        }
        for (int i = 0; i < pc; i++) {
                ASTnode *p = fndef->data.func_def.params[i];
                if (ssa_is_struct(s, p->data.var_decl.type_name)) {
                        Ref *rp = emalloc(sizeof(Ref));
                        *rp = prs[i]; // address directly, no copy
                        hashmap_put(s->slots, strdup(p->data.var_decl.name), rp);
                        continue;
                }
                int cls = quil_to_cls(p->data.var_decl.type_name);
                int psz = ssa_type_size(s, p->data.var_decl.type_name);
                Ref slot = il_create_alloc4(s->ilb, il_const_int_w(s->ilb, psz));
                Ref *rp = emalloc(sizeof(Ref));
                *rp = slot;                                               // PHeap so survives freeall()
                hashmap_put(s->slots, strdup(p->data.var_decl.name), rp); // slots name->Ref Kl
                if (psz == 1 || psz == 2) emit_store_elem(s, p->data.var_decl.type_name, prs[i], slot);
                else il_create_store(s->ilb, cls, prs[i], slot);          // store param to slot
        }
        // body BLOCK include/ast.h:191 -> emit_stmt() for each stmt
        for (int i = 0; i < fndef->data.func_def.body->data.blocks.count; i++) {
                emit_stmt(s, fndef->data.func_def.body->data.blocks.statements[i]);
        }
        if (!s->ilb->cur || s->ilb->cur->jmp.type == Jxxx) il_create_ret_void(s->ilb); // if no ret
        fn = il_finish(s->ilb);                                                        // nblk + rpo
        il_module_add_function(s->mod, fn);
        s->ilb = NULL;
        hashmap_clear(s->slots);
}

/* --- MAIN --- */
// selects the feather codegen target and optimization level from the CLI.
void ssagen_apply_options(const char *target, int level) {
        optlevel = level;
        if (target == NULL) {
                T = Deftgt; // host default (feather/config.h)
                return;
        }
        // mirrors feather's -t lookup
        Target *targets[] = {
            &T_amd64_sysv,
            &T_amd64_apple,
            &T_amd64_win,
            &T_arm64,
            &T_arm64_apple,
            &T_rv64,
            NULL,
        };
        for (int i = 0; targets[i] != NULL; i++) {
                if (strcmp(target, targets[i]->name) == 0) {
                        T = *targets[i];
                        return;
                }
        }
        quil_error(STAGE_FILE, ERR_INVALID_TARGET, target);
}
static void collect_externs(Ssagen *s, ASTnode *fndef) {
        if (fndef->type != NODE_FUNC_DEF || !fndef->data.func_def.is_extern) return;
        char *qname = s->cur_ns ? strf(PHeap, "%s::%s", s->cur_ns, fndef->data.func_def.name) : fndef->data.func_def.name;
        char *mg = mangle(qname);
        hashmap_put(s->externs, mg, (void *)1);
}
// recursive namespace walk (handles nested scope blocks); pass 0 = collect, 1 = emit
static void walk_ns(Ssagen *s, ASTnode *blk, int pass) {
        for (int j = 0; j < blk->data.blocks.count; j++) {
                ASTnode *inner = blk->data.blocks.statements[j];
                if (inner->type == NODE_NAMESPACE) {
                        char *old = s->cur_ns;
                        s->cur_ns = old ? strf(PHeap, "%s::%s", old, inner->data.namespace_decl.name) : strdup(inner->data.namespace_decl.name);
                        walk_ns(s, inner->data.namespace_decl.body, pass);
                        s->cur_ns = old;
                } else if (pass == 0) {
                        if (inner->type == NODE_STRUCT_DEF) collect_struct(s, inner);
                        else collect_externs(s, inner);
                } else if (inner->type == NODE_FUNC_DEF) {
                        emit_func(s, inner);
                }
        }
}
IlModule *ssagen_build(ASTnode *prog) {
        IlModule *mod = il_module_create();
        Ssagen s = {.mod = mod,
                    .slots = hashmap_create(hm_hash_str, hm_eq_str, NULL, NULL),
                    .externs = hashmap_create(hm_hash_str, hm_eq_str, NULL, NULL),
                    .struct_types = hashmap_create(hm_hash_str, hm_eq_str, free, ssa_struct_free),
                    .ilb = NULL};

        // first pass: struct layouts + extern prototypes (mangled) for SExt vs SGlo calls
        for (int i = 0; i < prog->data.program.count; i++) {
                ASTnode *stmt = prog->data.program.statements[i];
                if (stmt->type == NODE_NAMESPACE) {
                        char *old = s.cur_ns;
                        s.cur_ns = old ? strf(PHeap, "%s::%s", old, stmt->data.namespace_decl.name) : strdup(stmt->data.namespace_decl.name);
                        walk_ns(&s, stmt->data.namespace_decl.body, 0);
                        s.cur_ns = old;
                } else if (stmt->type == NODE_STRUCT_DEF) {
                        collect_struct(&s, stmt);
                } else if (stmt->type == NODE_FUNC_DEF) {
                        collect_externs(&s, stmt);
                }
        }
        s.cur_ns = NULL;

        for (int i = 0; i < prog->data.program.count; i++) {
                ASTnode *stmt = prog->data.program.statements[i];
                if (stmt->type == NODE_NAMESPACE) { // "scope" is referred as namespace
                        char *old = s.cur_ns;
                        s.cur_ns = old ? strf(PHeap, "%s::%s", old, stmt->data.namespace_decl.name) : strdup(stmt->data.namespace_decl.name);
                        walk_ns(&s, stmt->data.namespace_decl.body, 1);
                        s.cur_ns = old;
                } else if (stmt->type == NODE_FUNC_DEF) {
                        emit_func(&s, stmt);
                }
        }

        hashmap_free(s.slots);
        hashmap_free(s.externs);
        hashmap_free(s.struct_types);
        return mod;
}
void ssagen_emit_asm(IlModule *mod, FILE *out) {
        il_module_emit(mod, out);
}
void ssagen_emit_ssa(IlModule *mod, FILE *out) {
        // SSA text for inspection: data + functions via printfn() feather/parse.c:594
        for (uint i = 0; i < mod->ndat; i++) {
                IlData *d = mod->datas[i];
                for (uint j = 0; j < d->n; j++) {
                        // ssa data is same as asm data header: data $name = { ... }
                        // use feather's data printer via emitdat with ssa flag? For now reuse emitdat as ssa data is similar
                        // Instead, just emit via printfn's data path: use emitdat with out (ssa and asm share same data syntax)
                        emitdat(&d->items[j], out);
                }
                fputs("/* end data */\n\n", out);
        }
        for (uint i = 0; i < mod->nfn; i++) {
                printfn(mod->fns[i], out);
                fputs("\n", out);
        }
}

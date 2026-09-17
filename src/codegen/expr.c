/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

#include "internal.h"
#include <stdlib.h>

Ref emit_expr(Ssagen *s, ASTnode *n) {
        switch (n->type) {
        case NODE_INT_LITERAL: {
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
                static int str_id = 0;
                char name[32];
                snprintf(name, sizeof(name), ".str%d", str_id++);
                Lnk lnk = {0};
                IlData *data = il_data_begin(name, &lnk);
                il_data_add_str(data, DB, n->data.string_literal.value);
                il_data_add_b(data, 0);
                il_data_end(data);
                il_module_add_data(s->mod, data);
                return il_global_sym(s->ilb, name);
        }
        case NODE_IDENTIFIER: {
                bool found;
                Ref *slotp = hashmap_get(s->slots, n->data.identifier.name, &found);
                if (!found) quil_error(STAGE_CODEGEN, ERR_UNDECLARED_VAR, n->data.identifier.name);
                Ref slot = *slotp;
                return emit_load_elem(s, n->resolved_type, slot);
        }
        case NODE_ARRAY_ACCESS: {
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
                Ref addr = il_create_add_l(s->ilb, base, off_l);
                return emit_load_elem(s, et, addr);
        }
        case NODE_BINARY_EXPRESSION: {
                Ref l = emit_expr(s, n->data.binary_expression.left);
                Ref r = emit_expr(s, n->data.binary_expression.right);
                tokenType op = n->data.binary_expression.op;
                bool is_cmp = (op == TOKEN_EEQUAL || op == TOKEN_NEQUAL || op == TOKEN_LABRACKET ||
                               op == TOKEN_RABRACKET || op == TOKEN_LEQUAL || op == TOKEN_GEQUAL);
                if (is_cmp) {
                        int lhs_cls = quil_to_cls(n->data.binary_expression.left->resolved_type);
                        int rhs_cls = quil_to_cls(n->data.binary_expression.right->resolved_type);
                        int cmp_cls = (lhs_cls == Kl || rhs_cls == Kl) ? Kl : Kw;
                        bool u = is_unsigned_type(n->data.binary_expression.left->resolved_type) ||
                                 is_unsigned_type(n->data.binary_expression.right->resolved_type);
                        if (cmp_cls == Kl) {
                                if (lhs_cls == Kw) l = promote_kw_to_kl(s, n->data.binary_expression.left, l);
                                if (rhs_cls == Kw) r = promote_kw_to_kl(s, n->data.binary_expression.right, r);
                                if (op == TOKEN_EEQUAL) return il_create_icmp_eq_l(s->ilb, l, r);
                                if (op == TOKEN_NEQUAL) return il_create_icmp_ne_l(s->ilb, l, r);
                                if (op == TOKEN_LABRACKET) return u ? il_create_icmp_ult_l(s->ilb, l, r) : il_create_icmp_slt_l(s->ilb, l, r);
                                if (op == TOKEN_RABRACKET) return u ? il_create_icmp_ugt_l(s->ilb, l, r) : il_create_icmp_sgt_l(s->ilb, l, r);
                                if (op == TOKEN_LEQUAL) return u ? il_create_icmp_ule_l(s->ilb, l, r) : il_create_icmp_sle_l(s->ilb, l, r);
                                if (op == TOKEN_GEQUAL) return u ? il_create_icmp_uge_l(s->ilb, l, r) : il_create_icmp_sge_l(s->ilb, l, r);
                        } else {
                                if (op == TOKEN_EEQUAL) return il_create_icmp_eq_w(s->ilb, l, r);
                                if (op == TOKEN_NEQUAL) return il_create_icmp_ne_w(s->ilb, l, r);
                                if (op == TOKEN_LABRACKET) return u ? il_create_icmp_ult_w(s->ilb, l, r) : il_create_icmp_slt_w(s->ilb, l, r);
                                if (op == TOKEN_RABRACKET) return u ? il_create_icmp_ugt_w(s->ilb, l, r) : il_create_icmp_sgt_w(s->ilb, l, r);
                                if (op == TOKEN_LEQUAL) return u ? il_create_icmp_ule_w(s->ilb, l, r) : il_create_icmp_sle_w(s->ilb, l, r);
                                if (op == TOKEN_GEQUAL) return u ? il_create_icmp_uge_w(s->ilb, l, r) : il_create_icmp_sge_w(s->ilb, l, r);
                        }
                }
                int cls = quil_to_cls(n->resolved_type);
                bool u = is_unsigned_type(n->resolved_type);
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
                        int l_cls = quil_to_cls(n->data.binary_expression.left->resolved_type);
                        int r_cls = quil_to_cls(n->data.binary_expression.right->resolved_type);
                        if (l_cls == Kw) l = promote_kw_to_kl(s, n->data.binary_expression.left, l);
                        if (r_cls == Kw) r = promote_kw_to_kl(s, n->data.binary_expression.right, r);
                        switch (op) {
                        case TOKEN_PLUS: return il_create_add_l(s->ilb, l, r);
                        case TOKEN_MINUS: return il_create_sub_l(s->ilb, l, r);
                        case TOKEN_STAR: return il_create_mul_l(s->ilb, l, r);
                        case TOKEN_FSLASH: return u ? il_create_udiv_l(s->ilb, l, r) : il_create_div_l(s->ilb, l, r);
                        case TOKEN_PERCENT: return u ? il_create_urem_l(s->ilb, l, r) : il_create_rem_l(s->ilb, l, r);
                        case TOKEN_AND: return il_create_and_l(s->ilb, l, r);
                        case TOKEN_PIPE: return il_create_or_l(s->ilb, l, r);
                        case TOKEN_CARET: return il_create_xor_l(s->ilb, l, r);
                        default: break;
                        }
                        if (op == TOKEN_EEQUAL) return il_create_icmp_eq_l(s->ilb, l, r);
                        if (op == TOKEN_NEQUAL) return il_create_icmp_ne_l(s->ilb, l, r);
                } else {
                        switch (op) {
                        case TOKEN_PLUS: return il_create_add_w(s->ilb, l, r);
                        case TOKEN_MINUS: return il_create_sub_w(s->ilb, l, r);
                        case TOKEN_STAR: return il_create_mul_w(s->ilb, l, r);
                        case TOKEN_FSLASH: return u ? il_create_udiv_w(s->ilb, l, r) : il_create_div_w(s->ilb, l, r);
                        case TOKEN_PERCENT: return u ? il_create_urem_w(s->ilb, l, r) : il_create_rem_w(s->ilb, l, r);
                        case TOKEN_AND: return il_create_and_w(s->ilb, l, r);
                        case TOKEN_PIPE: return il_create_or_w(s->ilb, l, r);
                        case TOKEN_CARET: return il_create_xor_w(s->ilb, l, r);
                        default: break;
                        }
                        if (op == TOKEN_EEQUAL) return il_create_icmp_eq_w(s->ilb, l, r);
                        if (op == TOKEN_NEQUAL) return il_create_icmp_ne_w(s->ilb, l, r);
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
                        return il_create_icmp_eq_w(s->ilb, v, il_const_zero(s->ilb));
                }
                quil_error(STAGE_CODEGEN, ERR_UNKNOWN, lexer_token_type_to_string(op));
        }
        case NODE_TERNARY_EXPRESSION: {
                Ref cond = emit_expr(s, n->data.ternary_expression.condition);
                Blk *then_blk = il_create_block(s->ilb, "tern.then");
                Blk *else_blk = il_create_block(s->ilb, "tern.else");
                Blk *merge = il_create_block(s->ilb, "tern.merge");
                il_create_cond_br(s->ilb, cond, then_blk, else_blk);
                il_set_insert_point(s->ilb, then_blk);
                Ref tv = emit_expr(s, n->data.ternary_expression.then_expr);
                il_create_br(s->ilb, merge);
                il_set_insert_point(s->ilb, else_blk);
                Ref ev = emit_expr(s, n->data.ternary_expression.else_expr);
                il_create_br(s->ilb, merge);
                il_set_insert_point(s->ilb, merge);
                Blk *preds[] = {then_blk, else_blk};
                Ref vals[] = {tv, ev};
                int cls = quil_to_cls(n->resolved_type);
                if (cls == Kl) return il_create_phi_l(s->ilb, preds, vals, 2);
                if (cls == Kd) return il_create_phi_d(s->ilb, preds, vals, 2);
                if (cls == Ks) return il_create_phi_s(s->ilb, preds, vals, 2);
                return il_create_phi_w(s->ilb, preds, vals, 2);
        }
        case NODE_MEMBER_ACCESS: {
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
                bool ret_is_struct = rt && ssa_is_struct(s, rt);
                if (ret_is_struct) {
                        int sz = ssa_type_size(s, rt);
                        Ref tmp = il_create_alloc4(s->ilb, il_const_int_w(s->ilb, sz));
                        // func sig for arg promotion
                        funcSig *fs = NULL;
                        bool found_fs = false;
                        if (s->func_sigs) fs = hashmap_get(s->func_sigs, mangled, &found_fs);
                        Ref *call_args = emalloc(sizeof(Ref) * (size_t)(nargs + 1));
                        call_args[0] = tmp;
                        for (int i = 0; i < nargs; i++) {
                                ASTnode *a = n->data.func_call.args[i];
                                Ref ar;
                                if (a->resolved_type && ssa_is_struct(s, a->resolved_type)) {
                                        ar = emit_obj_addr(s, a);
                                } else {
                                        ar = emit_expr(s, a);
                                        if (found_fs && i < (int)fs->param_count) {
                                                int pcls = quil_to_cls(fs->param_types[i]);
                                                int acls = quil_to_cls(a->resolved_type);
                                                if (pcls == Kl && acls == Kw) ar = promote_kw_to_kl(s, a, ar);
                                        }
                                }
                                call_args[i + 1] = ar;
                        }
                        for (int i = 0; i < nargs + 1; i++) il_call_arg(s->ilb, call_args[i]);
                        Ins ci = {.op = Ocall, .cls = Kw, .to = R, .arg = {callee, R}};
                        addins(&s->ilb->cur->ins, &s->ilb->cur->nins, &ci);
                        free(call_args);
                        free(mangled);
                        return tmp;
                }
                // non-struct return: handle arg promotion for Kl params
                Ref *args = NULL;
                if (nargs > 0) {
                        args = emalloc(sizeof(Ref) * (size_t)nargs);
                        funcSig *fs = NULL;
                        bool found_fs = false;
                        if (s->func_sigs) fs = hashmap_get(s->func_sigs, mangled, &found_fs);
                        for (int i = 0; i < nargs; i++) {
                                ASTnode *a = n->data.func_call.args[i];
                                Ref ar;
                                if (a->resolved_type && ssa_is_struct(s, a->resolved_type)) {
                                        ar = emit_obj_addr(s, a);
                                } else {
                                        ar = emit_expr(s, a);
                                        if (found_fs && i < (int)fs->param_count) {
                                                int pcls = quil_to_cls(fs->param_types[i]);
                                                int acls = quil_to_cls(a->resolved_type);
                                                if (pcls == Kl && acls == Kw) ar = promote_kw_to_kl(s, a, ar);
                                                else if (pcls == Kw && acls == Kl) {
                                                        // trunc not needed for now
                                                }
                                        }
                                }
                                args[i] = ar;
                        }
                }
                if (is_void) {
                        for (int i = 0; i < nargs; i++) il_call_arg(s->ilb, args[i]);
                        Ins ci = {.op = Ocall, .cls = Kw, .to = R, .arg = {callee, R}};
                        addins(&s->ilb->cur->ins, &s->ilb->cur->nins, &ci);
                        free(mangled);
                        free(args);
                        return CON_Z;
                }
                int cls = quil_to_cls(rt);
                Ref res;
                if (cls == Kl) res = il_create_call_l(s->ilb, callee, args, nargs);
                else if (cls == Ks) res = il_create_call_s(s->ilb, callee, args, nargs);
                else if (cls == Kd) res = il_create_call_d(s->ilb, callee, args, nargs);
                else res = il_create_call_w(s->ilb, callee, args, nargs);
                free(mangled);
                free(args);
                return res;
        }
        default:
                quil_error(STAGE_CODEGEN, ERR_UNKNOWN, node_type_name(n->type));
        }
}

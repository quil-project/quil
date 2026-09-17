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

Ref emit_obj_addr(Ssagen *s, ASTnode *obj) {
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

void emit_stmt(Ssagen *s, ASTnode *n) {
        switch (n->type) {
        case NODE_VAR_DECL: {
                const char *t = n->data.var_decl.type_name;
                int cls = quil_to_cls(t);
                int sz = ssa_type_size(s, t);
                if (sz != 1 && sz != 2 && sz != 4 && sz != 8) cls = Kl;
                (void)cls;
                if (n->data.var_decl.is_array) sz *= n->data.var_decl.array_size;
                Ref slot = il_create_alloc4(s->ilb, il_const_int_w(s->ilb, sz));
                Ref *rp = emalloc(sizeof(Ref));
                *rp = slot;
                hashmap_put(s->slots, strdup(n->data.var_decl.name), rp);
                if (n->data.var_decl.value) {
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
                il_set_insert_point(s->ilb, then_blk);
                emit_stmt(s, n->data.if_stat.then_block);
                if (s->ilb->cur) il_create_br(s->ilb, merge);
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
                bool is_range = n->data.for_loop.init && !n->data.for_loop.condition && !n->data.for_loop.increment && n->data.for_loop.init->type != NODE_VAR_DECL;
                Blk *old_break = s->break_target;
                Blk *old_cont = s->continue_target;
                if (is_range) {
                        static int range_id = 0;
                        char name[16];
                        snprintf(name, sizeof(name), "%d", range_id++);
                        Ref ctr_slot = il_create_alloc4(s->ilb, il_const_int_w(s->ilb, 4));
                        Ref *rp = emalloc(sizeof(Ref));
                        *rp = ctr_slot;
                        hashmap_put(s->slots, strdup(name), rp);
                        il_create_store_w(s->ilb, il_const_int_w(s->ilb, 0), ctr_slot);
                        Ref range_val = emit_expr(s, n->data.for_loop.init);
                        Blk *cond_blk = il_create_block(s->ilb, "for.cond");
                        Blk *body_blk = il_create_block(s->ilb, "for.body");
                        Blk *inc_blk = il_create_block(s->ilb, "for.inc");
                        Blk *merge = il_create_block(s->ilb, "for.merge");
                        s->break_target = merge;
                        s->continue_target = inc_blk;
                        il_create_br(s->ilb, cond_blk);
                        il_set_insert_point(s->ilb, cond_blk);
                        Ref cur = il_create_load_w(s->ilb, ctr_slot);
                        Ref cond = il_create_icmp_slt_w(s->ilb, cur, range_val);
                        il_create_cond_br(s->ilb, cond, body_blk, merge);
                        il_set_insert_point(s->ilb, body_blk);
                        emit_stmt(s, n->data.for_loop.body);
                        if (s->ilb->cur) il_create_br(s->ilb, inc_blk);
                        il_set_insert_point(s->ilb, inc_blk);
                        Ref cur2 = il_create_load_w(s->ilb, ctr_slot);
                        Ref inc = il_create_add_w(s->ilb, cur2, il_const_int_w(s->ilb, 1));
                        il_create_store_w(s->ilb, inc, ctr_slot);
                        il_create_br(s->ilb, cond_blk);
                        il_set_insert_point(s->ilb, merge);
                        s->break_target = old_break;
                        s->continue_target = old_cont;
                } else {
                        if (n->data.for_loop.init) emit_stmt(s, n->data.for_loop.init);
                        Blk *cond_blk = il_create_block(s->ilb, "for.cond");
                        Blk *body_blk = il_create_block(s->ilb, "for.body");
                        Blk *inc_blk = il_create_block(s->ilb, "for.inc");
                        Blk *merge = il_create_block(s->ilb, "for.merge");
                        Blk *saved_break = s->break_target;
                        Blk *saved_cont = s->continue_target;
                        s->break_target = merge;
                        s->continue_target = inc_blk;
                        il_create_br(s->ilb, cond_blk);
                        il_set_insert_point(s->ilb, cond_blk);
                        if (n->data.for_loop.condition) {
                                Ref cond = emit_expr(s, n->data.for_loop.condition);
                                il_create_cond_br(s->ilb, cond, body_blk, merge);
                        } else {
                                il_create_br(s->ilb, body_blk);
                        }
                        il_set_insert_point(s->ilb, body_blk);
                        emit_stmt(s, n->data.for_loop.body);
                        if (s->ilb->cur) il_create_br(s->ilb, inc_blk);
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

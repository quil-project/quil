/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

/* .ssa code generator - main driver */

#include "internal.h"
#include <stdlib.h>

Target T;
extern Target T_amd64_sysv;
extern Target T_amd64_apple;
extern Target T_amd64_win;
extern Target T_arm64;
extern Target T_arm64_apple;
extern Target T_rv64;
int optlevel = 0;
char debug['Z' + 1];

void ssa_struct_free(void *p) {
        SsaStruct *sd = (SsaStruct *)p;
        if (!sd) return;
        for (int i = 0; i < sd->field_count; i++) free(sd->field_names[i]);
        free(sd->field_names);
        free(sd->field_offsets);
        free(sd);
}

char *ssa_struct_qname(Ssagen *s, const char *name) {
        if (!s->cur_ns) return strdup(name);
        size_t len = strlen(s->cur_ns) + 2 + strlen(name) + 1;
        char *q = malloc(len);
        snprintf(q, len, "%s::%s", s->cur_ns, name);
        return q;
}

void collect_struct(Ssagen *s, ASTnode *def) {
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

char *mangle(const char *qname) {
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

void emit_func(Ssagen *s, ASTnode *fndef) {
        if (fndef->data.func_def.is_extern) {
                return;
        }
        char *qname = s->cur_ns ? strf(PHeap, "%s::%s", s->cur_ns, fndef->data.func_def.name) : fndef->data.func_def.name;
        char *mangled = mangle(qname);
        qname = mangled;
        Lnk lnk = {.export = fndef->data.func_def.is_public || strcmp(fndef->data.func_def.name, "main") == 0};
        Fn *fn = il_create_function(qname, Kx, &lnk);
        s->ilb = il_create(fn);
        Blk *entry = il_create_block(s->ilb, "entry");
        il_set_insert_point(s->ilb, entry);
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
                        *rp = prs[i];
                        hashmap_put(s->slots, strdup(p->data.var_decl.name), rp);
                        continue;
                }
                int cls = quil_to_cls(p->data.var_decl.type_name);
                int psz = ssa_type_size(s, p->data.var_decl.type_name);
                Ref slot = il_create_alloc4(s->ilb, il_const_int_w(s->ilb, psz));
                Ref *rp = emalloc(sizeof(Ref));
                *rp = slot;
                hashmap_put(s->slots, strdup(p->data.var_decl.name), rp);
                if (psz == 1 || psz == 2) emit_store_elem(s, p->data.var_decl.type_name, prs[i], slot);
                else il_create_store(s->ilb, cls, prs[i], slot);
        }
        for (int i = 0; i < fndef->data.func_def.body->data.blocks.count; i++) {
                emit_stmt(s, fndef->data.func_def.body->data.blocks.statements[i]);
        }
        if (!s->ilb->cur || s->ilb->cur->jmp.type == Jxxx) {
                if (strcmp(qname, "main") == 0) il_create_ret_w(s->ilb, il_const_int_w(s->ilb, 0));
                else il_create_ret_void(s->ilb);
        }
        fn = il_finish(s->ilb);
        il_module_add_function(s->mod, fn);
        s->ilb = NULL;
        hashmap_clear(s->slots);
}

void ssagen_apply_options(const char *target, int level) {
        optlevel = level;
        if (target == NULL) {
                T = Deftgt;
                return;
        }
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
                if (stmt->type == NODE_NAMESPACE) {
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
        for (uint i = 0; i < mod->ndat; i++) {
                IlData *d = mod->datas[i];
                for (uint j = 0; j < d->n; j++) {
                        emitdat(&d->items[j], out);
                }
                fputs("/* end data */\n\n", out);
        }
        for (uint i = 0; i < mod->nfn; i++) {
                printfn(mod->fns[i], out);
                fputs("\n", out);
        }
}

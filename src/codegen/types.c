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

bool is_unsigned_type(const char *t) {
        if (!t) return false;
        return strncmp(t, "uint", 4) == 0;
}

Ref promote_kw_to_kl(Ssagen *s, ASTnode *node, Ref r) {
        if (node->type == NODE_INT_LITERAL) {
                return il_const_int_l(s->ilb, (int64_t)node->data.int_literal.value);
        }
        bool u = is_unsigned_type(node->resolved_type);
        return u ? il_create_extuw_l(s->ilb, r) : il_create_extsw_l(s->ilb, r);
}

int quil_to_cls(const char *t) {
        if (!t) return Kw;
        if (!strcmp(t, "int8") || !strcmp(t, "int16") || !strcmp(t, "int32") ||
            !strcmp(t, "bool") || !strcmp(t, "uint8") || !strcmp(t, "uint16") ||
            !strcmp(t, "uint32")) {
                return Kw;
        }
        if (!strcmp(t, "int64") || !strcmp(t, "uint64") || !strcmp(t, "string") ||
            !strcmp(t, "char*") || !strcmp(t, "char *")) {
                return Kl;
        }
        if (!strcmp(t, "char")) return Kw;
        if (!strcmp(t, "float32")) return Ks;
        if (!strcmp(t, "float64")) return Kd;
        return Kw;
}

int elem_size_of(const char *t) {
        if (!t) return 4;
        if (!strcmp(t, "int8") || !strcmp(t, "uint8") || !strcmp(t, "char") || !strcmp(t, "bool")) return 1;
        if (!strcmp(t, "int16") || !strcmp(t, "uint16")) return 2;
        if (!strcmp(t, "int32") || !strcmp(t, "uint32") || !strcmp(t, "float32")) return 4;
        if (!strcmp(t, "int64") || !strcmp(t, "uint64") || !strcmp(t, "float64") || !strcmp(t, "string")) return 8;
        if (t[strlen(t) - 1] == '*') return 8;
        return 4;
}

void emit_store_elem(Ssagen *s, const char *t, Ref v, Ref addr) {
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

Ref emit_load_elem(Ssagen *s, const char *t, Ref addr) {
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

int ssa_align_of(Ssagen *s, const char *t) {
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

bool ssa_is_struct(Ssagen *s, const char *t) {
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

int ssa_type_size(Ssagen *s, const char *t) {
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
        return 8;
}

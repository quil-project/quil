/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

#include "../../include/parser.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>

void parse_type(Parser *parser, char **out_type_name, char **out_element_type) {
        *out_type_name = NULL;
        *out_element_type = NULL;
        if (check(parser, TOKEN_VEC) && !(parser->current + 1 < parser->tokens->size &&
                                          parser->tokens->tokens[parser->current + 1].type == TOKEN_DCOLON)) {
                token t = peek(parser);
                quil_error_at(STAGE_PARSER, ERR_UNKNOWN, t.line, t.col, "vec has been removed, use array type 'int32[N]' (vec will be stdlib later)");
        }
        if (check(parser, TOKEN_ID) && strcmp(peek(parser).value, "void") == 0) {
                advance(parser);
                *out_type_name = "void";
        } else if (match(parser, TOKEN_INT8)) *out_type_name = "int8";
        else if (match(parser, TOKEN_INT16)) *out_type_name = "int16";
        else if (match(parser, TOKEN_INT32)) *out_type_name = "int32";
        else if (match(parser, TOKEN_INT64)) *out_type_name = "int64";
        else if (match(parser, TOKEN_UINT8)) *out_type_name = "uint8";
        else if (match(parser, TOKEN_UINT16)) *out_type_name = "uint16";
        else if (match(parser, TOKEN_UINT32)) *out_type_name = "uint32";
        else if (match(parser, TOKEN_UINT64)) *out_type_name = "uint64";
        else if (match(parser, TOKEN_FLOAT32)) *out_type_name = "float32";
        else if (match(parser, TOKEN_FLOAT64)) *out_type_name = "float64";
        else if (match(parser, TOKEN_CHAR)) *out_type_name = "char";
        else if (match(parser, TOKEN_STRING)) *out_type_name = "string";
        else if (match(parser, TOKEN_BOOL)) *out_type_name = "bool";
        else if (check(parser, TOKEN_ID) || check(parser, TOKEN_VEC)) {
                token t = advance(parser);
                size_t len = strlen(t.value) + 1;
                char *name = malloc(len);
                snprintf(name, len, "%s", t.value);
                while (match(parser, TOKEN_DCOLON)) {
                        token seg = consume_path_segment(parser, "identifier after '::'");
                        size_t nlen = strlen(name) + 2 + strlen(seg.value) + 1;
                        name = realloc(name, nlen);
                        strcat(name, "::");
                        strcat(name, seg.value);
                }
                *out_type_name = name;
        } else {
                token found = peek(parser);
                quil_expected_at(STAGE_PARSER, found.line, found.col, "a data type (int32, float64, etc.)", peek_display(parser));
        }
}

ASTnode *parse_declaration(Parser *parser) {
        char *data_type = NULL;
        char *element_type = NULL;
        parse_type(parser, &data_type, &element_type);
        bool is_array = false;
        int array_size = 0;
        if (match(parser, TOKEN_LSPAREN)) {
                token sz = consume(parser, TOKEN_INUM, "array size");
                array_size = sz.int_value;
                consume(parser, TOKEN_RSPAREN, "']' after array size");
                is_array = true;
        }
        token name_token = consume(parser, TOKEN_ID, "a variable name");
        char *var_name = name_token.value;
        ASTnode *initializer = NULL;
        if (match(parser, TOKEN_EQUAL)) {
                initializer = parse_expression(parser);
        }
        consume_end_of_statement(parser);
        ASTnode *decl = make_var_decl_node(data_type, NULL, var_name, initializer, is_array, array_size);
        ast_set_loc(decl, name_token.line, name_token.col);
        return decl;
}

ASTnode *parse_struct_def(Parser *parser) {
        token name = consume_path_segment(parser, "struct name after 'struct'");
        ASTnode *body = parse_block(parser);
        ASTnode *def = make_struct_def_node(name.value);
        ast_set_loc(def, name.line, name.col);
        for (int i = 0; i < body->data.blocks.count; i++) {
                ASTnode *f = body->data.blocks.statements[i];
                if (f->type != NODE_VAR_DECL || f->data.var_decl.value) {
                        quil_error_at(STAGE_PARSER, ERR_UNEXPECTED_TOKEN, f->line, f->col, "struct fields must be 'type name' with no init");
                }
                ast_add_struct_field(def, f);
        }
        free(body->data.blocks.statements);
        free(body);
        return def;
}

ASTnode *parse_func_def_param(Parser *parser) {
        char *type_name = NULL;
        char *element_type = NULL;
        parse_type(parser, &type_name, &element_type);
        bool is_array = false;
        int array_size = 0;
        if (match(parser, TOKEN_LSPAREN)) {
                token sz = consume(parser, TOKEN_INUM, "array size");
                array_size = sz.int_value;
                consume(parser, TOKEN_RSPAREN, "']' after array size");
                is_array = true;
        }
        token name_token = consume(parser, TOKEN_ID, "a parameter name");
        ASTnode *param = make_var_decl_node(type_name, NULL, name_token.value, NULL, is_array, array_size);
        ast_set_loc(param, name_token.line, name_token.col);
        return param;
}

ASTnode *parse_func_def(Parser *parser) {
        token func_name = consume(parser, TOKEN_ID, "function name");
        consume(parser, TOKEN_LRPAREN, "'(' after function name");
        ASTnode **params = NULL;
        int param_count = 0;
        int param_capacity = 0;
        if (!check(parser, TOKEN_RRPAREN)) {
                do {
                        if (param_count >= param_capacity) {
                                param_capacity = param_capacity == 0 ? 4 : param_capacity * 2;
                                params = realloc(params, sizeof(ASTnode *) * param_capacity);
                        }
                        params[param_count++] = parse_func_def_param(parser);
                } while (match(parser, TOKEN_COMMA));
        }
        consume(parser, TOKEN_RRPAREN, "')' after function parameters");
        char *return_type = NULL;
        char *ret_element = NULL;
        if (check(parser, TOKEN_ARROW)) {
                advance(parser);
                parse_type(parser, &return_type, &ret_element);
        } else {
                return_type = "void";
        }
        ASTnode *body = NULL;
        if (check(parser, TOKEN_LCPAREN)) {
                body = parse_block(parser);
        }
        ASTnode *def = make_func_def_node(return_type, func_name.value, params, param_count, body);
        return def;
}

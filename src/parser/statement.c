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

ASTnode *parse_statement(Parser *parser) {
        if (check(parser, TOKEN_STRUCT)) {
                match(parser, TOKEN_STRUCT);
                return parse_struct_def(parser);
        }
        if (check(parser, TOKEN_FN) || check(parser, TOKEN_PUBLIC) || check(parser, TOKEN_EXTERN)) {
                bool is_public = false, is_extern = false;
                if (match(parser, TOKEN_PUBLIC)) is_public = true;
                if (match(parser, TOKEN_EXTERN)) is_extern = true;
                if (!is_public && match(parser, TOKEN_PUBLIC)) is_public = true;
                if (!is_extern && match(parser, TOKEN_EXTERN)) is_extern = true;
                if (!match(parser, TOKEN_FN)) {
                        token t = peek(parser);
                        quil_expected_at(STAGE_PARSER, t.line, t.col, "'fn' after 'public'/'extern'", peek_display(parser));
                }
                ASTnode *fn = parse_func_def(parser);
                fn->data.func_def.is_public = is_public;
                fn->data.func_def.is_extern = is_extern;
                if (is_extern) {
                        if (fn->data.func_def.body) {
                                free_ast_node(fn->data.func_def.body);
                                fn->data.func_def.body = NULL;
                        }
                        consume_end_of_statement(parser);
                }
                return fn;
        }
        if (check(parser, TOKEN_CONST)) {
                return parse_declaration(parser);
        }
        if (check(parser, TOKEN_ID) && parser->current + 1 < (int)parser->tokens->size &&
            parser->tokens->tokens[parser->current + 1].type == TOKEN_COLON_EQUAL) {
                return parse_declaration(parser);
        }
        if (check(parser, TOKEN_INT8) || check(parser, TOKEN_INT16) ||
            check(parser, TOKEN_INT32) || check(parser, TOKEN_INT64) ||
            check(parser, TOKEN_UINT8) || check(parser, TOKEN_UINT16) ||
            check(parser, TOKEN_UINT32) || check(parser, TOKEN_UINT64) ||
            check(parser, TOKEN_FLOAT32) || check(parser, TOKEN_FLOAT64) ||
            check(parser, TOKEN_CHAR) || check(parser, TOKEN_STRING) ||
            check(parser, TOKEN_BOOL)) {
                return parse_declaration(parser);
        }
        if (check(parser, TOKEN_ID)) {
                size_t j = parser->current;
                size_t n = parser->tokens->size;
                while (j + 2 < n && parser->tokens->tokens[j + 1].type == TOKEN_DCOLON &&
                       parser->tokens->tokens[j + 2].type == TOKEN_ID) {
                        j += 2;
                }
                bool is_decl = false;
                if (j + 1 < n) {
                        tokenType after = parser->tokens->tokens[j + 1].type;
                        if (after == TOKEN_ID) is_decl = true;
                        else if (after == TOKEN_LSPAREN && j + 4 < n &&
                                 parser->tokens->tokens[j + 2].type == TOKEN_INUM &&
                                 parser->tokens->tokens[j + 3].type == TOKEN_RSPAREN &&
                                 parser->tokens->tokens[j + 4].type == TOKEN_ID) {
                                is_decl = true;
                        }
                }
                if (is_decl) {
                        return parse_declaration(parser);
                }
        }
        if (check(parser, TOKEN_VEC) && !(parser->current + 1 < parser->tokens->size &&
                                          parser->tokens->tokens[parser->current + 1].type == TOKEN_DCOLON)) {
                token t = peek(parser);
                quil_error_at(STAGE_PARSER, ERR_UNKNOWN, t.line, t.col, "vec has been removed, use array type 'int32[N]' (vec will be stdlib later)");
        }
        if (match(parser, TOKEN_IF)) {
                return parse_if_statement(parser);
        }
        if (match(parser, TOKEN_WHILE)) {
                return parse_while_statement(parser);
        }
        if (match(parser, TOKEN_FOR)) {
                return parse_for_statement(parser);
        }
        if (match(parser, TOKEN_RETURN)) {
                return parse_return_statement(parser);
        }
        if (match(parser, TOKEN_BREAK)) {
                token kw = parser->tokens->tokens[parser->current - 1];
                consume_end_of_statement(parser);
                ASTnode *node = create_ast_node(NODE_BREAK);
                ast_set_loc(node, kw.line, kw.col);
                return node;
        }
        if (match(parser, TOKEN_CONTINUE)) {
                token kw = parser->tokens->tokens[parser->current - 1];
                consume_end_of_statement(parser);
                ASTnode *node = create_ast_node(NODE_CONTINUE);
                ast_set_loc(node, kw.line, kw.col);
                return node;
        }
        if (match(parser, TOKEN_SCOPE)) {
                token ns = consume_path_segment(parser, "namespace name after 'scope'");
                ASTnode *body = parse_block(parser);
                return make_namespace_node(ns.value, body);
        }
        if (match(parser, TOKEN_ATSIGN)) {
                token atsign_tok = parser->tokens->tokens[parser->current - 1];
                token name_token = advance(parser);
                char *name = name_token.value;
                char *value = NULL;
                if (check(parser, TOKEN_ID) || check(parser, TOKEN_LIB_STDLIB)) {
                        value = advance(parser).value;
                }
                ASTnode *node = make_directive_node(name, value);
                ast_set_loc(node, atsign_tok.line, atsign_tok.col);
                return node;
        }
        if (match(parser, TOKEN_NLINE)) {
                return NULL;
        }
        ASTnode *expression = parse_expression(parser);
        consume_end_of_statement(parser);
        return expression;
}

ASTnode *parse_if_statement(Parser *parser) {
        token kw = parser->tokens->tokens[parser->current - 1];
        consume(parser, TOKEN_LRPAREN, "'('");
        ASTnode *condition = parse_expression(parser);
        consume(parser, TOKEN_RRPAREN, "')'");
        ASTnode *then_block = parse_block(parser);
        ASTnode *else_block = NULL;
        if (match(parser, TOKEN_ELSE)) {
                if (match(parser, TOKEN_IF)) {
                        else_block = parse_if_statement(parser);
                } else {
                        else_block = parse_block(parser);
                }
        }
        ASTnode *node = make_if_stat_node(condition, then_block, else_block);
        ast_set_loc(node, kw.line, kw.col);
        return node;
}

ASTnode *parse_while_statement(Parser *parser) {
        token kw = parser->tokens->tokens[parser->current - 1];
        consume(parser, TOKEN_LRPAREN, "'('");
        ASTnode *condition = parse_expression(parser);
        consume(parser, TOKEN_RRPAREN, "')'");
        ASTnode *body = parse_block(parser);
        ASTnode *node = make_while_node(condition, body);
        ast_set_loc(node, kw.line, kw.col);
        return node;
}

ASTnode *parse_for_statement(Parser *parser) {
        token kw = parser->tokens->tokens[parser->current - 1];
        consume(parser, TOKEN_LRPAREN, "'('");
        ASTnode *init = NULL;
        if (check(parser, TOKEN_INT8) || check(parser, TOKEN_INT16) ||
            check(parser, TOKEN_INT32) || check(parser, TOKEN_INT64) ||
            check(parser, TOKEN_UINT8) || check(parser, TOKEN_UINT16) ||
            check(parser, TOKEN_UINT32) || check(parser, TOKEN_UINT64) ||
            check(parser, TOKEN_FLOAT32) || check(parser, TOKEN_FLOAT64) ||
            check(parser, TOKEN_CHAR) || check(parser, TOKEN_STRING) ||
            check(parser, TOKEN_BOOL) || check(parser, TOKEN_VEC)) {
                init = parse_declaration(parser);
        } else if (check(parser, TOKEN_SEMICOLON)) {
                advance(parser);
        } else {
                ASTnode *range = parse_expression(parser);
                if (check(parser, TOKEN_RRPAREN)) {
                        consume(parser, TOKEN_RRPAREN, "')'");
                        ASTnode *body = parse_block(parser);
                        return make_for_node(range, NULL, NULL, body);
                }
                consume(parser, TOKEN_SEMICOLON, "';'");
                init = range;
        }
        ASTnode *condition = NULL;
        if (!check(parser, TOKEN_SEMICOLON)) {
                condition = parse_expression(parser);
        }
        consume(parser, TOKEN_SEMICOLON, "';'");
        ASTnode *inc = NULL;
        if (!check(parser, TOKEN_RRPAREN)) {
                inc = parse_expression(parser);
        }
        consume(parser, TOKEN_RRPAREN, "')'");
        ASTnode *body = parse_block(parser);
        ASTnode *node = make_for_node(init, condition, inc, body);
        ast_set_loc(node, kw.line, kw.col);
        return node;
}

ASTnode *parse_return_statement(Parser *parser) {
        token kw = parser->tokens->tokens[parser->current - 1];
        ASTnode *expression = parse_expression(parser);
        consume_end_of_statement(parser);
        ASTnode *node = create_ast_node(NODE_RETURN);
        node->data.returns.expression = expression;
        ast_set_loc(node, kw.line, kw.col);
        return node;
}

ASTnode *parse_block(Parser *parser) {
        consume(parser, TOKEN_LCPAREN, "'{'");
        ASTnode *block = create_ast_node(NODE_BLOCK);
        while (!check(parser, TOKEN_RCPAREN) && !check(parser, TOKEN_EOF)) {
                if (match(parser, TOKEN_NLINE)) continue;
                ast_add_statement(block, parse_statement(parser));
        }
        consume(parser, TOKEN_RCPAREN, "'}'");
        return block;
}

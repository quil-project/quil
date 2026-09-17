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

ASTnode *parse_primary(Parser *parser) {
        if (match(parser, TOKEN_INUM)) {
                token tok = parser->tokens->tokens[parser->current - 1];
                ASTnode *node = make_int_node(tok.int_value);
                ast_set_loc(node, tok.line, tok.col);
                return node;
        }
        if (match(parser, TOKEN_FNUM)) {
                token tok = parser->tokens->tokens[parser->current - 1];
                ASTnode *node = make_float_node(tok.float_value);
                ast_set_loc(node, tok.line, tok.col);
                return node;
        }
        if (match(parser, TOKEN_ID)) {
                token tok = parser->tokens->tokens[parser->current - 1];
                ASTnode *node = make_identifier_node(tok.value);
                ast_set_loc(node, tok.line, tok.col);
                return node;
        }
        if (match(parser, TOKEN_TRUE)) {
                token tok = parser->tokens->tokens[parser->current - 1];
                ASTnode *node = make_bool_node(true);
                ast_set_loc(node, tok.line, tok.col);
                return node;
        }
        if (match(parser, TOKEN_FALSE)) {
                token tok = parser->tokens->tokens[parser->current - 1];
                ASTnode *node = make_bool_node(false);
                ast_set_loc(node, tok.line, tok.col);
                return node;
        }
        if (match(parser, TOKEN_LRPAREN)) {
                ASTnode *expr = parse_expression(parser);
                consume(parser, TOKEN_RRPAREN, "')' after expression");
                return expr;
        }
        if (match(parser, TOKEN_DQUOTE) || match(parser, TOKEN_SQUOTE)) {
                token quote_tok = parser->tokens->tokens[parser->current - 1];
                tokenType quote = quote_tok.type;
                char *str_content = strdup("");
                int content_tokens = 0;
                while (!check(parser, TOKEN_DQUOTE) && !check(parser, TOKEN_SQUOTE) && !check(parser, TOKEN_EOF)) {
                        token t = advance(parser);
                        content_tokens++;
                        if (t.value) {
                                char *new_str = malloc(strlen(str_content) + strlen(t.value) + 1);
                                sprintf(new_str, "%s%s", str_content, t.value);
                                free(str_content);
                                str_content = new_str;
                        }
                }
                advance(parser);
                if (quote == TOKEN_SQUOTE && content_tokens == 1 && str_content[0] != '\0' && str_content[1] == '\0') {
                        char c = str_content[0];
                        free(str_content);
                        ASTnode *charnode = make_char_node(c);
                        ast_set_loc(charnode, quote_tok.line, quote_tok.col);
                        return charnode;
                }
                ASTnode *node = make_string_node(str_content);
                free(str_content);
                ast_set_loc(node, quote_tok.line, quote_tok.col);
                return node;
        }
        if (match(parser, TOKEN_LSPAREN)) {
                token bracket_tok = parser->tokens->tokens[parser->current - 1];
                ASTnode **elements = NULL;
                int count = 0;
                int capacity = 0;
                if (!check(parser, TOKEN_RSPAREN)) {
                        do {
                                ASTnode *elem = parse_expression(parser);
                                if (count >= capacity) {
                                        capacity = capacity == 0 ? 4 : capacity * 2;
                                        elements = realloc(elements, sizeof(ASTnode *) * capacity);
                                }
                                elements[count++] = elem;
                        } while (match(parser, TOKEN_COMMA));
                }
                consume(parser, TOKEN_RSPAREN, "']' after vec elements");
                ASTnode *list_node = make_list_literal_node(elements, count);
                ast_set_loc(list_node, bracket_tok.line, bracket_tok.col);
                return list_node;
        }
        token found = peek(parser);
        quil_error_at(STAGE_PARSER, ERR_UNEXPECTED_TOKEN, found.line, found.col, peek_display(parser));
}

ASTnode *parse_call(Parser *parser) {
        ASTnode *node;
        if (check(parser, TOKEN_VEC) && parser->current + 1 < parser->tokens->size &&
            parser->tokens->tokens[parser->current + 1].type == TOKEN_DCOLON) {
                token v = advance(parser);
                node = make_identifier_node(v.value);
                ast_set_loc(node, v.line, v.col);
        } else {
                node = parse_primary(parser);
        }
        if (node->type == NODE_IDENTIFIER) {
                int cap = 4, count = 1;
                char **segs = malloc(cap * sizeof(char *));
                segs[0] = strdup(node->data.identifier.name);
                int qline = node->line, qcol = node->col;
                while (match(parser, TOKEN_DCOLON)) {
                        token t = consume_path_segment(parser, "identifier after '::'");
                        if (count >= cap) {
                                cap *= 2;
                                segs = realloc(segs, cap * sizeof(char *));
                        }
                        segs[count++] = strdup(t.value);
                }
                if (count > 1) {
                        free_ast_node(node);
                        node = make_qualified_node(segs, count);
                        ast_set_loc(node, qline, qcol);
                        if (!check(parser, TOKEN_LRPAREN)) {
                                quil_error_at(STAGE_PARSER, ERR_INVALID_CALL_TARGET, qline, qcol, "qualified path 'std::foo' must be called as 'std::foo()'");
                        }
                } else {
                        free(segs[0]);
                        free(segs);
                }
        }
        while (true) {
                if (match(parser, TOKEN_LRPAREN)) {
                        if (node->type != NODE_IDENTIFIER && node->type != NODE_QUALIFIED) {
                                token trigger = parser->tokens->tokens[parser->current - 1];
                                quil_error_at(STAGE_PARSER, ERR_INVALID_CALL_TARGET, trigger.line, trigger.col, node_type_name(node->type));
                        }
                        int line = node->line;
                        int col = node->col;
                        char *name;
                        if (node->type == NODE_QUALIFIED) {
                                size_t len = 0;
                                for (int i = 0; i < node->data.qualified.count; i++) len += strlen(node->data.qualified.segments[i]) + 2;
                                name = malloc(len + 1);
                                name[0] = '\0';
                                for (int i = 0; i < node->data.qualified.count; i++) {
                                        if (i) strcat(name, "::");
                                        strcat(name, node->data.qualified.segments[i]);
                                }
                        } else {
                                name = strdup(node->data.identifier.name);
                        }
                        free_ast_node(node);
                        ASTnode *call = make_func_call_node(name, NULL, 0);
                        ast_set_loc(call, line, col);
                        free(name);
                        if (!check(parser, TOKEN_RRPAREN)) {
                                do {
                                        ast_add_arg(call, parse_expression(parser));
                                } while (match(parser, TOKEN_COMMA));
                        }
                        consume(parser, TOKEN_RRPAREN, "')' after arguments");
                        node = call;
                } else if (match(parser, TOKEN_LSPAREN)) {
                        ASTnode *index = parse_expression(parser);
                        consume(parser, TOKEN_RSPAREN, "']' after index");
                        ASTnode *access = create_ast_node(NODE_ARRAY_ACCESS);
                        access->data.array_access.name = node->data.identifier.name;
                        access->data.array_access.index = index;
                        ast_set_loc(access, node->line, node->col);
                        node = access;
                } else if (match(parser, TOKEN_DOT)) {
                        token m = consume(parser, TOKEN_ID, "a member name after '.'");
                        ASTnode **args = NULL;
                        int arg_count = 0;
                        if (match(parser, TOKEN_LRPAREN)) {
                                if (!check(parser, TOKEN_RRPAREN)) {
                                        do {
                                                ast_add_member_arg(&args, &arg_count, parse_expression(parser));
                                        } while (match(parser, TOKEN_COMMA));
                                }
                                consume(parser, TOKEN_RRPAREN, "')' after arguments");
                        }
                        node = make_member_access_node(node, m.value, args, arg_count);
                        ast_set_loc(node, m.line, m.col);
                } else if (match(parser, TOKEN_PPLUS)) {
                        if (node->type != NODE_IDENTIFIER) {
                                token trigger = parser->tokens->tokens[parser->current - 1];
                                quil_error_at(STAGE_PARSER, ERR_INVALID_ASSIGN_TARGET, trigger.line, trigger.col, node_type_name(node->type));
                        }
                        int line = node->line;
                        int col = node->col;
                        char *name = strdup(node->data.identifier.name);
                        free_ast_node(node);
                        ASTnode *inc = make_binary_node(make_identifier_node(name), TOKEN_PLUS, make_int_node(1));
                        node = make_assign_node(name, inc);
                        ast_set_loc(node, line, col);
                        free(name);
                } else if (match(parser, TOKEN_MMINUS)) {
                        if (node->type != NODE_IDENTIFIER) {
                                token trigger = parser->tokens->tokens[parser->current - 1];
                                quil_error_at(STAGE_PARSER, ERR_INVALID_ASSIGN_TARGET, trigger.line, trigger.col, node_type_name(node->type));
                        }
                        int line = node->line;
                        int col = node->col;
                        char *name = strdup(node->data.identifier.name);
                        free_ast_node(node);
                        ASTnode *dec = make_binary_node(make_identifier_node(name), TOKEN_MINUS, make_int_node(1));
                        node = make_assign_node(name, dec);
                        ast_set_loc(node, line, col);
                        free(name);
                } else {
                        break;
                }
        }
        return node;
}

ASTnode *parse_unary(Parser *parser) {
        if (match(parser, TOKEN_EXCLAMATION) || match(parser, TOKEN_MINUS)) {
                token op_tok = parser->tokens->tokens[parser->current - 1];
                tokenType op = op_tok.type;
                ASTnode *left = parse_unary(parser);
                ASTnode *node = make_unary_node(op, left);
                ast_set_loc(node, op_tok.line, op_tok.col);
                return node;
        }
        return parse_call(parser);
}

ASTnode *parse_factors(Parser *parser) {
        ASTnode *node = parse_unary(parser);
        while (match(parser, TOKEN_STAR) || match(parser, TOKEN_FSLASH) || match(parser, TOKEN_PERCENT)) {
                token op_tok = parser->tokens->tokens[parser->current - 1];
                tokenType op = op_tok.type;
                ASTnode *right = parse_call(parser);
                node = make_binary_node(node, op, right);
                ast_set_loc(node, op_tok.line, op_tok.col);
        }
        return node;
}

ASTnode *parse_term(Parser *parser) {
        ASTnode *node = parse_factors(parser);
        while (match(parser, TOKEN_PLUS) || match(parser, TOKEN_MINUS)) {
                token op_tok = parser->tokens->tokens[parser->current - 1];
                tokenType op = op_tok.type;
                ASTnode *right = parse_factors(parser);
                node = make_binary_node(node, op, right);
                ast_set_loc(node, op_tok.line, op_tok.col);
        }
        return node;
}

ASTnode *parse_comparison(Parser *parser) {
        ASTnode *node = parse_term(parser);
        while (match(parser, TOKEN_LABRACKET) || match(parser, TOKEN_RABRACKET) ||
               match(parser, TOKEN_LEQUAL) || match(parser, TOKEN_GEQUAL)) {
                token op_tok = parser->tokens->tokens[parser->current - 1];
                tokenType op = op_tok.type;
                ASTnode *right = parse_term(parser);
                node = make_binary_node(node, op, right);
                ast_set_loc(node, op_tok.line, op_tok.col);
        }
        return node;
}

ASTnode *parse_equality(Parser *parser) {
        ASTnode *node = parse_comparison(parser);
        while (match(parser, TOKEN_EEQUAL) || match(parser, TOKEN_NEQUAL)) {
                token op_tok = parser->tokens->tokens[parser->current - 1];
                tokenType op = op_tok.type;
                ASTnode *right = parse_comparison(parser);
                node = make_binary_node(node, op, right);
                ast_set_loc(node, op_tok.line, op_tok.col);
        }
        return node;
}

ASTnode *parse_logical_and(Parser *parser) {
        ASTnode *node = parse_equality(parser);
        while (match(parser, TOKEN_AND)) {
                token op_tok = parser->tokens->tokens[parser->current - 1];
                tokenType op = op_tok.type;
                ASTnode *right = parse_equality(parser);
                node = make_binary_node(node, op, right);
                ast_set_loc(node, op_tok.line, op_tok.col);
        }
        return node;
}

ASTnode *parse_logical_or(Parser *parser) {
        ASTnode *node = parse_logical_and(parser);
        while (match(parser, TOKEN_OR)) {
                token op_tok = parser->tokens->tokens[parser->current - 1];
                tokenType op = op_tok.type;
                ASTnode *right = parse_logical_and(parser);
                node = make_binary_node(node, op, right);
                ast_set_loc(node, op_tok.line, op_tok.col);
        }
        return node;
}

ASTnode *parse_ternary(Parser *parser) {
        ASTnode *node = parse_logical_or(parser);
        ASTnode *then_expr = NULL;
        ASTnode *else_expr = NULL;
        if (match(parser, TOKEN_QUESTION)) {
                token q_tok = parser->tokens->tokens[parser->current - 1];
                then_expr = parse_ternary(parser);
                consume(parser, TOKEN_COLON, "':'");
                else_expr = parse_ternary(parser);
                node = make_ternary_node(node, then_expr, else_expr);
                ast_set_loc(node, q_tok.line, q_tok.col);
        }
        return node;
}

ASTnode *parse_assignment(Parser *parser) {
        ASTnode *node = parse_ternary(parser);
        if (match(parser, TOKEN_EQUAL)) {
                token trigger = parser->tokens->tokens[parser->current - 1];
                ASTnode *value = parse_assignment(parser);
                if (node->type == NODE_ARRAY_ACCESS) {
                        char *name = node->data.array_access.name;
                        ASTnode *index = node->data.array_access.index;
                        int line = node->line;
                        int col = node->col;
                        node->data.array_access.name = NULL;
                        node->data.array_access.index = NULL;
                        free_ast_node(node);
                        ASTnode *assign = make_array_assign_node(name, index, value);
                        ast_set_loc(assign, line, col);
                        free(name);
                        return assign;
                }
                if (node->type == NODE_MEMBER_ACCESS) {
                        if (node->data.member_access.arg_count > 0) {
                                quil_error_at(STAGE_PARSER, ERR_INVALID_ASSIGN_TARGET, trigger.line, trigger.col, node_type_name(node->type));
                        }
                        ASTnode *obj = node->data.member_access.object;
                        char *member = node->data.member_access.member;
                        int line = node->line;
                        int col = node->col;
                        node->data.member_access.object = NULL;
                        node->data.member_access.member = NULL;
                        free_ast_node(node);
                        ASTnode *assign = make_member_assign_node(obj, member, value);
                        ast_set_loc(assign, line, col);
                        free(member);
                        return assign;
                }
                if (node->type != NODE_IDENTIFIER) {
                        quil_error_at(STAGE_PARSER, ERR_INVALID_ASSIGN_TARGET, trigger.line, trigger.col, node_type_name(node->type));
                }
                int line = node->line;
                int col = node->col;
                char *name = strdup(node->data.identifier.name);
                free_ast_node(node);
                ASTnode *assign = make_assign_node(name, value);
                ast_set_loc(assign, line, col);
                free(name);
                return assign;
        }
        if (match(parser, TOKEN_PEQUAL) || match(parser, TOKEN_MEQUAL) ||
            match(parser, TOKEN_SEQUAL) || match(parser, TOKEN_FSEQUAL) ||
            match(parser, TOKEN_PCEQUAL)) {
                token trigger = parser->tokens->tokens[parser->current - 1];
                ASTnode *value = parse_assignment(parser);
                if (node->type != NODE_IDENTIFIER) {
                        quil_error_at(STAGE_PARSER, ERR_INVALID_ASSIGN_TARGET, trigger.line, trigger.col, node_type_name(node->type));
                }
                int line = node->line;
                int col = node->col;
                char *name = strdup(node->data.identifier.name);
                free_ast_node(node);
                tokenType op = TOKEN_PLUS;
                switch (trigger.type) {
                case TOKEN_MEQUAL: op = TOKEN_MINUS; break;
                case TOKEN_SEQUAL: op = TOKEN_STAR; break;
                case TOKEN_FSEQUAL: op = TOKEN_FSLASH; break;
                case TOKEN_PCEQUAL: op = TOKEN_PERCENT; break;
                default: break;
                }
                ASTnode *sum = make_binary_node(make_identifier_node(name), op, value);
                ASTnode *assign = make_assign_node(name, sum);
                ast_set_loc(assign, line, col);
                free(name);
                return assign;
        }
        return node;
}

ASTnode *parse_expression(Parser *parser) {
        return parse_assignment(parser);
}

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
#include <string.h>

ASTnode *quil_parse(token_list *tokens) {
        Parser parser = {tokens, 0};
        return parse_program(&parser);
}
ASTnode *parse_line(token_list *tokens) {
        Parser parser = {tokens, 0};
        return parse_statement(&parser);
}

ASTnode *parse_program(Parser *parser) {
        ASTnode *program = create_ast_node(NODE_PROGRAM);
        while (!check(parser, TOKEN_EOF)) {
                if (match(parser, TOKEN_NLINE)) {
                        continue;
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
                        ast_add_statement(program, fn);
                        continue;
                }
                if (check(parser, TOKEN_STRUCT)) {
                        match(parser, TOKEN_STRUCT);
                        ast_add_statement(program, parse_struct_def(parser));
                        continue;
                }
                ast_add_statement(program, parse_statement(parser));
        }
        return program;
}

// a path segment: plain identifier or the reserved `vec` (usable as a scope/struct name and inside `::` chains)
token consume_path_segment(Parser *parser, const char *expected) {
        if (check(parser, TOKEN_VEC)) {
                return advance(parser);
        }
        return consume(parser, TOKEN_ID, expected);
}

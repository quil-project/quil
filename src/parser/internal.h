/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

#ifndef QUIL_PARSER_INTERNAL_H
#define QUIL_PARSER_INTERNAL_H

#include "../../include/parser.h"

// Shared helpers used across parser submodules (formerly static in parser.c)
token consume_path_segment(Parser *parser, const char *expected);
void parse_type(Parser *parser, char **out_type_name, char **out_element_type);

// Parser helper functions moved from src/helper.c
// Implemented as static inline here to keep parser helpers self-contained.
// Public names (peek, advance, etc.) declared in include/parser.h are
// redirected to these internal inlines for all TUs that include this header.
static inline token parser_internal_peek(Parser *parser) {
        if (parser->current >= parser->tokens->size) {
                return parser->tokens->tokens[parser->tokens->size - 1];
        }
        return parser->tokens->tokens[parser->current];
}
static inline token parser_internal_advance(Parser *parser) {
        if (parser->current < parser->tokens->size) {
                parser->current++;
        }
        return parser->tokens->tokens[parser->current - 1];
}
static inline bool parser_internal_check(Parser *parser, tokenType type) {
        return parser_internal_peek(parser).type == type;
}
static inline bool parser_internal_match(Parser *parser, tokenType type) {
        if (parser_internal_check(parser, type)) {
                parser_internal_advance(parser);
                return true;
        }
        return false;
}
static inline const char *parser_internal_peek_display(Parser *parser) {
        token t = parser_internal_peek(parser);
        if (t.value) {
                return t.value;
        }
        static char buf[64];
        switch (t.type) {
        case TOKEN_INUM:
                snprintf(buf, sizeof(buf), "%lld", (long long)t.int_value);
                break;
        case TOKEN_FNUM:
                snprintf(buf, sizeof(buf), "%g", t.float_value);
                break;
        case TOKEN_NLINE:
                return "newline";
        case TOKEN_EOF:
                return "end of file";
        default:
                return lexer_token_type_to_string(t.type);
        }
        return buf;
}
static inline token parser_internal_consume(Parser *parser, tokenType type, const char *expected) {
        if (parser_internal_check(parser, type)) {
                return parser_internal_advance(parser);
        }
        token found = parser_internal_peek(parser);
        quil_expected_at(STAGE_PARSER, found.line, found.col, expected, parser_internal_peek_display(parser));
        return found;
}
static inline void parser_internal_consume_end_of_statement(Parser *parser) {
        if (parser_internal_match(parser, TOKEN_SEMICOLON) || parser_internal_match(parser, TOKEN_NLINE)) {
                return;
        }
        if (parser_internal_check(parser, TOKEN_EOF)) {
                return;
        }
        token found = parser_internal_peek(parser);
        quil_expected_at(STAGE_PARSER, found.line, found.col, "';' or newline after statement", parser_internal_peek_display(parser));
}

// Redirect public helper names to the internal inlines for parser TUs
#undef peek
#undef advance
#undef check
#undef match
#undef peek_display
#undef consume
#undef consume_end_of_statement
#define peek parser_internal_peek
#define advance parser_internal_advance
#define check parser_internal_check
#define match parser_internal_match
#define peek_display parser_internal_peek_display
#define consume parser_internal_consume
#define consume_end_of_statement parser_internal_consume_end_of_statement

#endif // QUIL_PARSER_INTERNAL_H

/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "error.h"
#include "lexer.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
        token_list *tokens;
        size_t current;
} Parser;

// Core parser functions
ASTnode *quil_parse(token_list *tokens);
ASTnode *parse_line(token_list *tokens);
// Internal parsing functions (recursive descent)
ASTnode *parse_program(Parser *parser);
// Statement level parsing
ASTnode *parse_statement(Parser *parser);
ASTnode *parse_if_statement(Parser *parser);
ASTnode *parse_while_statement(Parser *parser);
ASTnode *parse_for_statement(Parser *parser);
ASTnode *parse_return_statement(Parser *parser);
ASTnode *parse_read_statement(Parser *parser);
ASTnode *parse_declaration(Parser *parser);
ASTnode *parse_block(Parser *parser);
// Function parsing
ASTnode *parse_func_def(Parser *parser);
ASTnode *parse_func_def_param(Parser *parser);
ASTnode *parse_struct_def(Parser *parser);
// Expression parsing
ASTnode *parse_primary(Parser *parser);
ASTnode *parse_call(Parser *parser);        // for functions calls like func(parameters) and member access like arr[0]
ASTnode *parse_unary(Parser *parser);       // for ! and - in front of values
ASTnode *parse_factors(Parser *parser);     // for multiplication and division
ASTnode *parse_term(Parser *parser);        // for addition and subtraction
ASTnode *parse_assignment(Parser *parser);  // assignment parsing, e.g. x = 10
ASTnode *parse_ternary(Parser *parser);     // NODE_TERNARY: cond ? then_expr : else_expr
ASTnode *parse_logical_or(Parser *parser);  // logical or ||
ASTnode *parse_logical_and(Parser *parser); // logical and &&
ASTnode *parse_equality(Parser *parser);    // equality ==, !=
ASTnode *parse_comparison(Parser *parser);  // for <, >, <=, >=
ASTnode *parse_expression(Parser *parser);  // Entry point, calling parser functions in order

// helper functions
// NOTE: below functions are defined in src/helper.c
token peek(Parser *parser);
token advance(Parser *parser);
bool check(Parser *parser, tokenType type);
bool match(Parser *parser, tokenType type);
token consume(Parser *parser, tokenType type, const char *expected);
void consume_end_of_statement(Parser *parser);
// returns a human-readable description of the current token for error messages
const char *peek_display(Parser *parser);

#endif // !PARSER_H

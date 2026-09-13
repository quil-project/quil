/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

#ifndef LEXER_H
#define LEXER_H

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
        TOKEN_ID,      // normal tokens maybe strings and characters inside print function, also can be veriables.
        TOKEN_INUM,    // intager numbers: 10, 50, 42 etc.
        TOKEN_FNUM,    // floating point numbers: 3.14, 0.5 etc.
        TOKEN_WSPACE,  // white space inside quotes
        TOKEN_QSTRING, // quoted string, tokens inside quotes
        // Libreries
        TOKEN_LIB_STDLIB, // if user does not include `@for engine` then user need to include `@import stdlib` to use standard librery features
        TOKEN_LIB_MATH,   // if user does not include `@for engine` then user need to include `@import stdlib` to use standard math features
        // Keywords
        TOKEN_IMPORT,   // token import, can be used at the starting of the file after atsign to import files and libreries
        TOKEN_IF,       // kewword if
        TOKEN_ELSE,     // Keyword else
        TOKEN_WHILE,    // keyword while
        TOKEN_FOR,      // keyword for
        TOKEN_RETURN,   // keyword return
        TOKEN_BREAK,    // keyword break
        TOKEN_CONTINUE, // keyword continue
        TOKEN_FN,       // keyword fn, defines a function
        TOKEN_SCOPE,    // keyword scope, defines a namespace/scope block
        TOKEN_PUBLIC,   // keyword public, marks fn as exported
        TOKEN_EXTERN,   // keyword extern, declares fn without body (prototype)
        // Data types
        TOKEN_BOOL,    // boolean data type
        TOKEN_INT8,    // 8-bit signed integer
        TOKEN_INT16,   // 16-bit signed integer
        TOKEN_INT32,   // 32-bit signed integer
        TOKEN_INT64,   // 64-bit signed integer
        TOKEN_UINT8,   // 8-bit unsigned integer
        TOKEN_UINT16,  // 16-bit unsigned integer
        TOKEN_UINT32,  // 32-bit unsigned integer
        TOKEN_UINT64,  // 64-bit unsigned integer
        TOKEN_FLOAT32, // 32-bit IEEE float
        TOKEN_FLOAT64, // 64-bit IEEE float
        TOKEN_CHAR,    // char datatype
        TOKEN_STRING,  // string datatype, basically char array in the transpiled code
        TOKEN_VEC,     // vec datatype (removed, kept as reserved to error)
        TOKEN_TRUE,    // boolean literal true
        TOKEN_FALSE,   // boolean literal false
        // single character tokens
        TOKEN_DOT,         // "." dot
        TOKEN_EQUAL,       // "=" equal
        TOKEN_PLUS,        // "+" plus
        TOKEN_MINUS,       // "-" mius
        TOKEN_STAR,        // "*" star, can be used for multiplications
        TOKEN_FSLASH,      // "/" forward slash, can be use for division
        TOKEN_BSLASH,      // "\" back slash
        TOKEN_COMMA,       // "," comma
        TOKEN_SEMICOLON,   // ";" semicolon
        TOKEN_COLON,       // ":" colon (for ternary ? :)
        TOKEN_DCOLON,      // "::" double colon (namespace qualifier)
        TOKEN_ARROW,       // "->" arrow (return-type separator)
        TOKEN_LRPAREN,     // "(" left round parenthesis
        TOKEN_RRPAREN,     // ")" right round parenthesis
        TOKEN_LCPAREN,     // "{" left curly parenthesis
        TOKEN_RCPAREN,     // "}" right curly parenthesis
        TOKEN_LSPAREN,     // "[" left stright parenthesis
        TOKEN_RSPAREN,     // "]" right straight parenthesis
        TOKEN_LABRACKET,   // "<" left angle bracket
        TOKEN_RABRACKET,   // ">" right angle bracket
        TOKEN_EXCLAMATION, // "!" exclamation mark
        TOKEN_ATSIGN,      // "@" at sign
        TOKEN_DOLLAR,      // "$" dollar sign
        TOKEN_PERCENT,     // "%" persent
        TOKEN_CARET,       // "^" caret also known as upward arrow
        TOKEN_AMPERSAND,   // "&" ampersand
        TOKEN_AND,         // "&&" and operator
        TOKEN_PIPE,        // "|" pipe
        TOKEN_OR,          // "||" or operator
        TOKEN_QUESTION,    // "?" question mark
        TOKEN_TILDE,       // "~" tilde
        TOKEN_SQUOTE,      // ' single quotation mark
        TOKEN_DQUOTE,      // " double quotation mark
        // Multi-character operators
        TOKEN_EEQUAL,  // "==" double equal
        TOKEN_NEQUAL,  // "!=" not equal
        TOKEN_LEQUAL,  // "<=" less than or equal
        TOKEN_GEQUAL,  // ">=" greater than or equal
        TOKEN_PPLUS,   // "++" increment
        TOKEN_PEQUAL,  // "+=" add and assign
        TOKEN_MMINUS,  // "--" decrement
        TOKEN_MEQUAL,  // "-=" subtract and assign
        TOKEN_SEQUAL,  // "*=" multiply and assign
        TOKEN_FSEQUAL, // "/=" divide and assign
        TOKEN_PCEQUAL, // "%=" modulo and assign
        // special character tokens
        TOKEN_NTERMINATOR, // '\0' null terminator
        TOKEN_NLINE,       // '\n' newline character
        TOKEN_UNLINE,      // '\n' user defined newline character
        TOKEN_TAB,         // '\t' tab
        // EOF and unknown
        TOKEN_EOF,    // End of file
        TOKEN_UNKNOWN // unknown
} tokenType;

// Holds the token
typedef struct {
        tokenType type;
        char *value;        // stores the token as string
        int int_value;      // stores the token as intager value
        double float_value; // stores the token as float value
        int line;           // 1-based line of the token's first character
        int col;            // 1-based column of the token's first character
} token;

// Holds a list of tokens
typedef struct {
        token *tokens;
        size_t size;
        size_t capacity;
} token_list;

/* --- FUNCTIONS --- */
// NOTE: below 4 functions are helper functions and will be defined in src/helper.c
void token_list_init(token_list *list);         // initializes the token
void token_list_add(token_list *list, token t); // adds a token to the list
void token_list_free(token_list *list);         // frees the list so we dont cause memory leak
// NOTE: 3 functions below are main function and will be defined in src/lexer.c
token lexer_tokenizer(FILE *buffer);
token lexer_tokenize_numbers(FILE *buffer);
token lexer_tokenize_words(FILE *buffer);
// NOTE: these two functions are for debugging purposes and temporary.
const char *lexer_token_type_to_string(tokenType type);
void lexer_print_token(token t);

#endif // !LEXER_H

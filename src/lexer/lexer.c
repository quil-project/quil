/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

#include "../../include/lexer.h"
#include "../../include/mode.h"
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

// quote mode, will be true if we encounter TOKEN_SQUOTE or TOKEN_DQUOTE for first time;
// if we encounter those tokens second time quote mode will be disabled.
bool SQUOTE_MODE = false; // single quote mode
bool DQUOTE_MODE = false; // double quote mode

// --- position tracking ---
// g_line/g_col hold the position where the NEXT character will be placed.
// g_prev_line/g_prev_col remember the position before the most recently read
// character, so an ungetc() can undo its position change. This works because
// the lexer only ever pushes back the single, most recently read character.
static int g_line = 1;
static int g_col = 1;
static int g_prev_line = 1;
static int g_prev_col = 1;

static int lexer_getc(FILE *buffer) {
        int ch = fgetc(buffer);
        g_prev_line = g_line;
        g_prev_col = g_col;
        if (ch == '\n') {
                g_line++;
                g_col = 1;
        } else if (ch != EOF) {
                g_col++;
        }
        return ch;
}

static int lexer_ungetc(int ch, FILE *buffer) {
        g_line = g_prev_line;
        g_col = g_prev_col;
        return ungetc(ch, buffer);
}

// This function tokenizes all the words, keywords and characters in the provided .quil file.
// The tokens are then handed to the parser to be grammer checked.
token lexer_tokenizer(FILE *buffer) {
        token tokens;
        tokens.value = NULL;
        int ch = lexer_getc(buffer);

        // Ignore white spaces, untill we hit EOF or newline
        while (ch != EOF && isspace(ch) && !(DQUOTE_MODE || SQUOTE_MODE) && ch != '\n') {
                ch = lexer_getc(buffer);
        }

        // the current character is the token's first character, so its position
        // is the one saved by the last lexer_getc() call.
        tokens.line = g_prev_line;
        tokens.col = g_prev_col;

        // Identifies whispace as valid token when they are inside quotes or counted as strings.
        if (isspace(ch) && (DQUOTE_MODE || SQUOTE_MODE)) {
                tokens.type = TOKEN_WSPACE;
                tokens.value = strdup(" ");
                return tokens;
        }

        // check for end of file (EOF)
        if (ch == EOF) {
                tokens.type = TOKEN_EOF;
                tokens.value = strdup("EOF");
                return tokens;
        }

        // ID, keywords, veriable and others handling.
        if (isalpha(ch) || ch == '_') {
                lexer_ungetc(ch, buffer);
                return lexer_tokenize_words(buffer);
        }

        // - number handling -
        // In quote mode numbers are part of the string content, so tokenize them
        // like any other word (keeps the text in `value`) instead of as a number.
        if ((isdigit(ch) || ch == '.') && !(SQUOTE_MODE || DQUOTE_MODE)) {
                lexer_ungetc(ch, buffer);
                return lexer_tokenize_numbers(buffer);
        }
        if (isdigit(ch) && (SQUOTE_MODE || DQUOTE_MODE)) {
                lexer_ungetc(ch, buffer);
                return lexer_tokenize_words(buffer);
        }

        // --------------- SINGLE CHARACTER TOKEN ---------------
        switch (ch) {
        case '=':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("=");
                } else {
                        ch = lexer_getc(buffer);
                        if (ch == ' ' || ch != '=') {
                                lexer_ungetc(ch, buffer);
                                tokens.type = TOKEN_EQUAL;
                                tokens.value = strdup("=");
                        } else if (ch == '=') {
                                tokens.type = TOKEN_EEQUAL;
                                tokens.value = strdup("==");
                        }
                }
                break;
        case '+':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("+");
                } else {
                        ch = lexer_getc(buffer);
                        if (ch == '+') {
                                tokens.type = TOKEN_PPLUS;
                                tokens.value = strdup("++");
                        } else if (ch == '=') {
                                tokens.type = TOKEN_PEQUAL;
                                tokens.value = strdup("+=");
                        } else {
                                lexer_ungetc(ch, buffer);
                                tokens.type = TOKEN_PLUS;
                                tokens.value = strdup("+");
                        }
                }
                break;
        case '-':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("-");
                } else {
                        ch = lexer_getc(buffer);
                        if (ch == '-') {
                                tokens.type = TOKEN_MMINUS;
                                tokens.value = strdup("--");
                        } else if (ch == '=') {
                                tokens.type = TOKEN_MEQUAL;
                                tokens.value = strdup("-=");
                        } else if (ch == '>') {
                                tokens.type = TOKEN_ARROW;
                                tokens.value = strdup("->");
                        } else {
                                lexer_ungetc(ch, buffer);
                                tokens.type = TOKEN_MINUS;
                                tokens.value = strdup("-");
                        }
                }
                break;
        case '*':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("*");
                } else {
                        ch = lexer_getc(buffer);
                        if (ch == '=') {
                                tokens.type = TOKEN_SEQUAL;
                                tokens.value = strdup("*=");
                        } else {
                                lexer_ungetc(ch, buffer);
                                tokens.type = TOKEN_STAR;
                                tokens.value = strdup("*");
                        }
                }
                break;
        case '/':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("/");
                } else {
                        ch = lexer_getc(buffer);
                        if (ch == '/') {
                                // // line comment: discard to newline or EOF
                                int c;
                                do {
                                        c = lexer_getc(buffer);
                                } while (c != '\n' && c != EOF);
                                if (c == EOF) {
                                        tokens.type = TOKEN_EOF;
                                        tokens.value = strdup("EOF");
                                } else {
                                        tokens.type = TOKEN_NLINE;
                                        tokens.value = strdup("\\n");
                                }
                        } else if (ch == '=') {
                                tokens.type = TOKEN_FSEQUAL;
                                tokens.value = strdup("/=");
                        } else {
                                lexer_ungetc(ch, buffer);
                                tokens.type = TOKEN_FSLASH;
                                tokens.value = strdup("/");
                        }
                }
                break;
        case ',':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup(",");
                } else {
                        tokens.type = TOKEN_COMMA;
                        tokens.value = strdup(",");
                }
                break;
        case ';':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup(";");
                } else {
                        tokens.type = TOKEN_SEMICOLON;
                        tokens.value = strdup(";");
                }
                break;
        case ':':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup(":");
                } else {
                        ch = lexer_getc(buffer);
                        if (ch == ':') {
                                tokens.type = TOKEN_DCOLON;
                                tokens.value = strdup("::");
                        } else if (ch == '=') {
                                tokens.type = TOKEN_COLON_EQUAL;
                                tokens.value = strdup(":=");
                        } else {
                                if (ch != EOF) lexer_ungetc(ch, buffer);
                                tokens.type = TOKEN_COLON;
                                tokens.value = strdup(":");
                        }
                }
                break;
        case '(':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("(");
                } else {
                        tokens.type = TOKEN_LRPAREN;
                        tokens.value = strdup("(");
                }
                break;
        case ')':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup(")");
                } else {
                        tokens.type = TOKEN_RRPAREN;
                        tokens.value = strdup(")");
                }
                break;
        case '{':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("{");
                } else {
                        tokens.type = TOKEN_LCPAREN;
                        tokens.value = strdup("{");
                }
                break;
        case '}':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("}");
                } else {
                        tokens.type = TOKEN_RCPAREN;
                        tokens.value = strdup("}");
                }
                break;
        case '[':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("[");
                } else {
                        tokens.type = TOKEN_LSPAREN;
                        tokens.value = strdup("[");
                }
                break;
        case ']':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("]");
                } else {
                        tokens.type = TOKEN_RSPAREN;
                        tokens.value = strdup("]");
                }
                break;
        case '<':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("<");
                } else {
                        ch = lexer_getc(buffer);
                        if (ch == ' ' || ch != '=') {
                                lexer_ungetc(ch, buffer);
                                tokens.type = TOKEN_LABRACKET;
                                tokens.value = strdup("<");
                        } else if (ch == '=') {
                                tokens.type = TOKEN_LEQUAL;
                                tokens.value = strdup("<=");
                        }
                }
                break;
        case '>':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup(">");
                } else {
                        ch = lexer_getc(buffer);
                        if (ch == ' ' || ch != '=') {
                                lexer_ungetc(ch, buffer);
                                tokens.type = TOKEN_RABRACKET;
                                tokens.value = strdup(">");
                        } else if (ch == '=') {
                                tokens.type = TOKEN_GEQUAL;
                                tokens.value = strdup(">=");
                        }
                }
                break;
        case '!':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("!");
                } else {
                        ch = lexer_getc(buffer);
                        if (ch == ' ' || ch != '=') {
                                lexer_ungetc(ch, buffer);
                                tokens.type = TOKEN_EXCLAMATION;
                                tokens.value = strdup("!");
                        } else if (ch == '=') {
                                tokens.type = TOKEN_NEQUAL;
                                tokens.value = strdup("!=");
                        }
                }
                break;
        case '@':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("@");
                } else {
                        tokens.type = TOKEN_ATSIGN;
                        tokens.value = strdup("@");
                }
                break;
        case '#':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("#");
                } else {
                        tokens.type = TOKEN_UNKNOWN;
                        tokens.value = strdup("#");
                }
                break;
        case '$':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("$");
                } else {
                        tokens.type = TOKEN_DOLLAR;
                        tokens.value = strdup("$");
                }
                break;
        case '%':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("%");
                } else {
                        ch = lexer_getc(buffer);
                        if (ch == '=') {
                                tokens.type = TOKEN_PCEQUAL;
                                tokens.value = strdup("%=");
                        } else {
                                lexer_ungetc(ch, buffer);
                                tokens.type = TOKEN_PERCENT;
                                tokens.value = strdup("%");
                        }
                }
                break;
        case '^':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("^");
                } else {
                        tokens.type = TOKEN_CARET;
                        tokens.value = strdup("^");
                }
                break;
        case '&':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("&");
                } else {
                        ch = lexer_getc(buffer);
                        if (ch == ' ' || ch != '&') {
                                lexer_ungetc(ch, buffer);
                                tokens.type = TOKEN_AMPERSAND;
                                tokens.value = strdup("&");
                        } else if (ch == '&') {
                                tokens.type = TOKEN_AND;
                                tokens.value = strdup("&&");
                        }
                }
                break;
        case '|':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("|");
                } else {
                        ch = lexer_getc(buffer);
                        if (ch == ' ' || ch != '|') {
                                lexer_ungetc(ch, buffer);
                                tokens.type = TOKEN_PIPE;
                                tokens.value = strdup("|");
                        } else if (ch == '|') {
                                tokens.type = TOKEN_OR;
                                tokens.value = strdup("||");
                        }
                }
                break;

        case '?':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("?");
                } else {
                        tokens.type = TOKEN_QUESTION;
                        tokens.value = strdup("?");
                }
                break;
        case '~':
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("~");
                } else {
                        tokens.type = TOKEN_TILDE;
                        tokens.value = strdup("~");
                }
                break;
        // SPECIAL CHARACTER TOKENS
        case '\'':
                // enabling or disabling SQUOTE_MODE
                if (SQUOTE_MODE) {
                        SQUOTE_MODE = false;
                } else if (!DQUOTE_MODE) {
                        SQUOTE_MODE = true;
                }

                // Identifying the token
                if (DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("'");
                } else {
                        tokens.type = TOKEN_SQUOTE;
                        tokens.value = strdup("'");
                }
                break;
        case '\"':
                // enabling or disabling DQUOTE_MODE
                if (DQUOTE_MODE) {
                        DQUOTE_MODE = false;
                } else if (!SQUOTE_MODE) {
                        DQUOTE_MODE = true;
                }

                // Identifying the token
                if (SQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = strdup("\"");
                } else {
                        tokens.type = TOKEN_DQUOTE;
                        tokens.value = strdup("\"");
                }
                break;
        // NOTE: quote modes should not be implemented for 3 cases below
        case '\0':
                tokens.type = TOKEN_NTERMINATOR;
                tokens.value = strdup("\\0");
                break;
        case '\n':
                tokens.type = TOKEN_NLINE;
                tokens.value = strdup("\\n");
                break;
        case '\t':
                tokens.type = TOKEN_TAB;
                tokens.value = strdup("\\t");
                break;
        // For physically written newline, tab and null terminator;
        case '\\':
                ch = lexer_getc(buffer);
                if (ch == 'n') {
                        tokens.type = TOKEN_UNLINE;
                        tokens.value = strdup("\\n");
                } else if (ch == 't') {
                        tokens.type = TOKEN_TAB;
                        tokens.value = strdup("\\t");
                } else if (ch == '0') {
                        tokens.type = TOKEN_NTERMINATOR;
                        tokens.value = strdup("\\0");
                } else {
                        lexer_ungetc(ch, buffer);
                        if (SQUOTE_MODE || DQUOTE_MODE) {
                                tokens.type = TOKEN_QSTRING;
                                tokens.value = strdup("\\");
                        } else {
                                tokens.type = TOKEN_BSLASH;
                                tokens.value = strdup("\\");
                        }
                }
                break;
        default:
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                        tokens.value = (char *)malloc(2);
                        tokens.value[0] = (char)ch;
                        tokens.value[1] = '\0';
                } else {
                        tokens.type = TOKEN_UNKNOWN;
                        tokens.value = (char *)malloc(2);
                        tokens.value[0] = (char)ch;
                        tokens.value[1] = '\0';
                }
                break;
        }
        // ------------------------------------------------------

        return tokens;
}

// ID, keywords, veriable and others handling.
// Triggered when the current char we are reading is a letter or a non-numerical character.
token lexer_tokenize_words(FILE *buffer) {
        int ch, i = 0; // i is also used for size ditermination.
        token tokens;
        tokens.value = NULL;

        int capacity = 2; // capacity will be doubled everytime the size is close to the capacity.
        char *char_buffer = malloc(capacity);

        // - reading the characters and putting them in the char_buffer -
        ch = lexer_getc(buffer);
        // the position saved by the first read is the start of this token.
        tokens.line = g_prev_line;
        tokens.col = g_prev_col;

        while (ch != EOF && (isalnum(ch) || ch == '_')) {
                if (i + 1 >= capacity) {
                        capacity *= 2;
                        char *tmp = realloc(char_buffer, capacity);
                        if (tmp == NULL) {
                                free(char_buffer);
                                tokens.value = NULL;
                                return tokens;
                        }
                        char_buffer = tmp;
                }

                char_buffer[i++] = (char)ch;
                ch = lexer_getc(buffer);
        }
        char_buffer[i] = '\0';

        if (ch != EOF) {
                lexer_ungetc(ch, buffer);
        }

        // - Identifying and handling ID, veriable and datatypes -
        tokens.value = char_buffer;

        if (strcmp(char_buffer, "int8") == 0) {
                tokens.type = SQUOTE_MODE || DQUOTE_MODE ? TOKEN_QSTRING : TOKEN_INT8;
        } else if (strcmp(char_buffer, "int16") == 0) {
                tokens.type = SQUOTE_MODE || DQUOTE_MODE ? TOKEN_QSTRING : TOKEN_INT16;
        } else if (strcmp(char_buffer, "int32") == 0) {
                tokens.type = SQUOTE_MODE || DQUOTE_MODE ? TOKEN_QSTRING : TOKEN_INT32;
        } else if (strcmp(char_buffer, "int64") == 0) {
                tokens.type = SQUOTE_MODE || DQUOTE_MODE ? TOKEN_QSTRING : TOKEN_INT64;
        } else if (strcmp(char_buffer, "uint8") == 0) {
                tokens.type = SQUOTE_MODE || DQUOTE_MODE ? TOKEN_QSTRING : TOKEN_UINT8;
        } else if (strcmp(char_buffer, "uint16") == 0) {
                tokens.type = SQUOTE_MODE || DQUOTE_MODE ? TOKEN_QSTRING : TOKEN_UINT16;
        } else if (strcmp(char_buffer, "uint32") == 0) {
                tokens.type = SQUOTE_MODE || DQUOTE_MODE ? TOKEN_QSTRING : TOKEN_UINT32;
        } else if (strcmp(char_buffer, "uint64") == 0) {
                tokens.type = SQUOTE_MODE || DQUOTE_MODE ? TOKEN_QSTRING : TOKEN_UINT64;
        } else if (strcmp(char_buffer, "float32") == 0) {
                tokens.type = SQUOTE_MODE || DQUOTE_MODE ? TOKEN_QSTRING : TOKEN_FLOAT32;
        } else if (strcmp(char_buffer, "float64") == 0) {
                tokens.type = SQUOTE_MODE || DQUOTE_MODE ? TOKEN_QSTRING : TOKEN_FLOAT64;
        } else if (strcmp(char_buffer, "if") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_IF;
                }
        } else if (strcmp(char_buffer, "else") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_ELSE;
                }
        } else if (strcmp(char_buffer, "while") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_WHILE;
                }
        } else if (strcmp(char_buffer, "for") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_FOR;
                }
        } else if (strcmp(char_buffer, "return") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_RETURN;
                }
        } else if (strcmp(char_buffer, "import") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_IMPORT;
                }
        } else if (strcmp(char_buffer, "break") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_BREAK;
                }
        } else if (strcmp(char_buffer, "continue") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_CONTINUE;
                }
        } else if (strcmp(char_buffer, "fn") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_FN;
                }
        } else if (strcmp(char_buffer, "scope") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_SCOPE;
                }
        } else if (strcmp(char_buffer, "public") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_PUBLIC;
                }
        } else if (strcmp(char_buffer, "extern") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_EXTERN;
                }
        } else if (strcmp(char_buffer, "struct") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_STRUCT;
                }
        } else if (strcmp(char_buffer, "const") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_CONST;
                }
        } else if (strcmp(char_buffer, "long") == 0) {
                tokens.type = TOKEN_ID;
        } else if (strcmp(char_buffer, "short") == 0) {
                tokens.type = TOKEN_ID;
        } else if (strcmp(char_buffer, "signed") == 0) {
                tokens.type = TOKEN_ID;
        } else if (strcmp(char_buffer, "unsigned") == 0) {
                tokens.type = TOKEN_ID;
        } else if (strcmp(char_buffer, "bool") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_BOOL;
                }
        } else if (strcmp(char_buffer, "double") == 0) {
                tokens.type = TOKEN_ID;
        } else if (strcmp(char_buffer, "char") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_CHAR;
                }
        } else if (strcmp(char_buffer, "string") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_STRING;
                }
        } else if (strcmp(char_buffer, "vec") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_VEC;
                }
        } else if (strcmp(char_buffer, "true") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_TRUE;
                }
        } else if (strcmp(char_buffer, "false") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_FALSE;
                }
        }
        /* LIBRERIES */
        else if (strcmp(char_buffer, "stdlib") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_LIB_STDLIB;
                }
        } else if (strcmp(char_buffer, "math") == 0) {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_LIB_MATH;
                }
        }
        /* If nothing matches */
        else {
                if (SQUOTE_MODE || DQUOTE_MODE) {
                        tokens.type = TOKEN_QSTRING;
                } else {
                        tokens.type = TOKEN_ID;
                }
        }

        return tokens;
}

// number tokenization, triggered when stumbled upon a digit.
token lexer_tokenize_numbers(FILE *buffer) {
        int ch, i = 0; // i is also used for size ditermination
        token tokens;
        tokens.value = NULL;

        int capacity = 2;
        char *char_buffer = malloc(capacity);
        // would be true if there is a dot in the number.
        bool is_float = false;

        // - reading the numbers and putting them in the char_buffer -
        ch = lexer_getc(buffer);
        // the position saved by the first read is the start of this token.
        tokens.line = g_prev_line;
        tokens.col = g_prev_col;

        while (ch != EOF && (isdigit(ch) || ch == '.')) {
                if (i + 1 >= capacity) {
                        capacity *= 2;
                        char *tmp = realloc(char_buffer, capacity);
                        if (tmp == NULL) {
                                free(char_buffer);
                                tokens.value = NULL;
                                return tokens;
                        }
                        char_buffer = tmp;
                }

                if (ch == '.') {
                        // break if we hit second dot in the sngle number.
                        if (is_float) {
                                break;
                        }
                        is_float = true;
                }

                char_buffer[i++] = (char)ch;
                ch = lexer_getc(buffer);
        }
        char_buffer[i] = '\0';

        // unreading the last non digit digit char read by the loop before
        if (ch != EOF) {
                lexer_ungetc(ch, buffer);
        }

        // - Number handling -
        // int_value and float_value members are only used for storing numbers which is handled by this block of code below
        if (strcmp(char_buffer, ".") == 0) {
                free(char_buffer);
                tokens.type = TOKEN_DOT;
                tokens.value = strdup(".");
                return tokens;
        }
        if (is_float) {
                tokens.type = TOKEN_FNUM;
                tokens.float_value = atof(char_buffer);
                free(char_buffer);
                return tokens;
        } else {
                tokens.type = TOKEN_INUM;
                // use 64-bit conversion to avoid truncation of large literals (e.g. uint64 max)
                tokens.int_value = (int64_t)strtoll(char_buffer, NULL, 10);
                // fallback to unsigned interpretation if strtoll overflows (value > INT64_MAX)
                // strtoull preserves bit pattern for 18446744073709551615
                if (tokens.int_value == INT64_MAX || tokens.int_value == INT64_MIN) {
                        // re-parse as unsigned to get full 64-bit range, cast preserves bits
                        unsigned long long uv = strtoull(char_buffer, NULL, 10);
                        tokens.int_value = (int64_t)uv;
                }
                free(char_buffer);
                return tokens;
        }

        return tokens;
}

// NOTE: this function is temporary and is only for debugging purposes.
const char *lexer_token_type_to_string(tokenType type) {
        switch (type) {
        case TOKEN_INUM:
                return "TOKEN_INUM";
        case TOKEN_FNUM:
                return "TOKEN_FNUM";
        case TOKEN_WSPACE:
                return "TOKEN_WSPACE";
        case TOKEN_QSTRING:
                return "TOKEN_QSTRING";
        case TOKEN_ID:
                return "TOKEN_ID";
        case TOKEN_IMPORT:
                return "TOKEN_IMPORT";
        case TOKEN_LIB_STDLIB:
                return "TOKEN_LIB_STDLIB";
        case TOKEN_LIB_MATH:
                return "TOKEN_LIB_MATH";
        case TOKEN_IF:
                return "TOKEN_IF";
        case TOKEN_ELSE:
                return "TOKEN_ELSE";
        case TOKEN_WHILE:
                return "TOKEN_WHILE";
        case TOKEN_FOR:
                return "TOKEN_FOR";
        case TOKEN_RETURN:
                return "TOKEN_RETURN";
        case TOKEN_BREAK:
                return "TOKEN_BREAK";
        case TOKEN_CONTINUE:
                return "TOKEN_CONTINUE";
        case TOKEN_FN:
                return "TOKEN_FN";
        case TOKEN_BOOL:
                return "TOKEN_BOOL";
        case TOKEN_INT8:
                return "TOKEN_INT8";
        case TOKEN_INT16:
                return "TOKEN_INT16";
        case TOKEN_INT32:
                return "TOKEN_INT32";
        case TOKEN_INT64:
                return "TOKEN_INT64";
        case TOKEN_UINT8:
                return "TOKEN_UINT8";
        case TOKEN_UINT16:
                return "TOKEN_UINT16";
        case TOKEN_UINT32:
                return "TOKEN_UINT32";
        case TOKEN_UINT64:
                return "TOKEN_UINT64";
        case TOKEN_FLOAT32:
                return "TOKEN_FLOAT32";
        case TOKEN_FLOAT64:
                return "TOKEN_FLOAT64";
        case TOKEN_CHAR:
                return "TOKEN_CHAR";
        case TOKEN_STRING:
                return "TOKEN_STRING";
        case TOKEN_VEC:
                return "TOKEN_VEC";
        case TOKEN_PUBLIC:
                return "TOKEN_PUBLIC";
        case TOKEN_EXTERN:
                return "TOKEN_EXTERN";
        case TOKEN_STRUCT:
                return "TOKEN_STRUCT";
        case TOKEN_CONST:
                return "TOKEN_CONST";
        case TOKEN_TRUE:
                return "TOKEN_TRUE";
        case TOKEN_FALSE:
                return "TOKEN_FALSE";
        case TOKEN_DOT:
                return "TOKEN_DOT";
        case TOKEN_EQUAL:
                return "TOKEN_EQUAL";
        case TOKEN_PLUS:
                return "TOKEN_PLUS";
        case TOKEN_MINUS:
                return "TOKEN_MINUS";
        case TOKEN_STAR:
                return "TOKEN_STAR";
        case TOKEN_FSLASH:
                return "TOKEN_FSLASH";
        case TOKEN_BSLASH:
                return "TOKEN_BSLASH";
        case TOKEN_COMMA:
                return "TOKEN_COMMA";
        case TOKEN_SEMICOLON:
                return "TOKEN_SEMICOLON";
        case TOKEN_COLON:
                return "TOKEN_COLON";
        case TOKEN_COLON_EQUAL:
                return "TOKEN_COLON_EQUAL";
        case TOKEN_DCOLON:
                return "TOKEN_DCOLON";
        case TOKEN_ARROW:
                return "TOKEN_ARROW";
        case TOKEN_SCOPE:
                return "TOKEN_SCOPE";
        case TOKEN_LRPAREN:
                return "TOKEN_LRPAREN";
        case TOKEN_RRPAREN:
                return "TOKEN_RRPAREN";
        case TOKEN_LCPAREN:
                return "TOKEN_LCPAREN";
        case TOKEN_RCPAREN:
                return "TOKEN_RCPAREN";
        case TOKEN_LSPAREN:
                return "TOKEN_LSPAREN";
        case TOKEN_RSPAREN:
                return "TOKEN_RSPAREN";
        case TOKEN_LABRACKET:
                return "TOKEN_LABRACKET";
        case TOKEN_RABRACKET:
                return "TOKEN_RABRACKET";
        case TOKEN_EXCLAMATION:
                return "TOKEN_EXCLAMATION";
        case TOKEN_ATSIGN:
                return "TOKEN_ATSIGN";
        case TOKEN_DOLLAR:
                return "TOKEN_DOLLAR";
        case TOKEN_PERCENT:
                return "TOKEN_PERCENT";
        case TOKEN_CARET:
                return "TOKEN_CARET";
        case TOKEN_AMPERSAND:
                return "TOKEN_AMPERSAND";
        case TOKEN_AND:
                return "TOKEN_AND";
        case TOKEN_PIPE:
                return "TOKEN_PIPE";
        case TOKEN_OR:
                return "TOKEN_OR";
        case TOKEN_QUESTION:
                return "TOKEN_QUESTION";
        case TOKEN_TILDE:
                return "TOKEN_TILDE";
        case TOKEN_SQUOTE:
                return "TOKEN_SQUOTE";
        case TOKEN_DQUOTE:
                return "TOKEN_DQUOTE";
        case TOKEN_EEQUAL:
                return "TOKEN_EEQUAL";
        case TOKEN_NEQUAL:
                return "TOKEN_NEQUAL";
        case TOKEN_LEQUAL:
                return "TOKEN_LEQUAL";
        case TOKEN_GEQUAL:
                return "TOKEN_GEQUAL";
        case TOKEN_PPLUS:
                return "TOKEN_PPLUS";
        case TOKEN_PEQUAL:
                return "TOKEN_PEQUAL";
        case TOKEN_MMINUS:
                return "TOKEN_MMINUS";
        case TOKEN_MEQUAL:
                return "TOKEN_MEQUAL";
        case TOKEN_SEQUAL:
                return "TOKEN_SEQUAL";
        case TOKEN_FSEQUAL:
                return "TOKEN_FSEQUAL";
        case TOKEN_PCEQUAL:
                return "TOKEN_PCEQUAL";
        case TOKEN_NTERMINATOR:
                return "TOKEN_NTERMINATOR";
        case TOKEN_NLINE:
                return "TOKEN_NLINE";
        case TOKEN_UNLINE:
                return "TOKEN_UNLINE";
        case TOKEN_TAB:
                return "TOKEN_TAB";
        case TOKEN_EOF:
                return "TOKEN_EOF";
        case TOKEN_UNKNOWN:
                return "TOKEN_UNKNOWN";
        default:
                return "UNKNOWN_TYPE";
        }
}

// NOTE: this function is temporary and is only for debugging purposes.
void lexer_print_token(token t) {
        printf("%-20s: ", lexer_token_type_to_string(t.type));
        if (t.type == TOKEN_INUM) {
                printf("%lld\n", (long long)t.int_value);
        } else if (t.type == TOKEN_FNUM) {
                printf("%f\n", t.float_value);
        } else if (t.value) {
                printf("%s\n", t.value);
        } else {
                printf("\n");
        }
}

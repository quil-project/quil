/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

#ifndef ERROR_H
#define ERROR_H

#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>

/*
 * @enum ErrorStage
 * @brief Identifies which part of the pipeline reported a message.
 */
typedef enum {
        STAGE_LEXER,    // Lexer / tokenizer
        STAGE_PARSER,   // Parser / syntax analysis
        STAGE_SEMANTIC, // Semantic analysis
        STAGE_CODEGEN,  // Code generator
        STAGE_UPDATER,  // Self-update pipeline
        STAGE_FILE,     // File / CLI handling
        STAGE_INTERNAL  // Internal errors (allocation, etc.)
} ErrorStage;

/*
 * @enum ErrorCode
 * @brief All possible errors the language can report.
 */
typedef enum {
        // Parser errors
        ERR_UNEXPECTED_TOKEN,         // unexpected token
        ERR_EXPECTED_TYPE,            // expected a data type
        ERR_EXPECTED_ID,              // expected an identifier
        ERR_MISSING_SEMICOLON,        // missing ';' after a statement
        ERR_MISSING_PAREN,            // missing parenthesis
        ERR_INVALID_MODIFIER,         // invalid type modifier
        ERR_INVALID_CALL_TARGET,      // tried to call a non-function
        ERR_INVALID_ASSIGN_TARGET,    // invalid assignment target
        ERR_MISSING_END_OF_STATEMENT, // expected ';' or newline

        // Semantic analysis errors
        ERR_UNDECLARED_VAR,  // used a variable that was never declared
        ERR_UNDECLARED_TYPE, // used a type that was never declared
        ERR_REDECLARED_VAR,  // declared a variable already in scope
        ERR_UNDECLARED_FUNC, // called a function that was never declared
        ERR_ARG_COUNT,       // function called with wrong number of args
        ERR_DUPLICATED_FUNC, // function name defined more than once
        ERR_DUPLICATED_TYPE, // struct name defined more than once
        ERR_NO_MAIN,         // program has no 'fn main()' entry point
        ERR_MAIN_NOT_PUBLIC, // main must be public
        ERR_TOP_LEVEL_STMT,  // statement at file scope that isn't a declaration

        // File / CLI errors
        ERR_NO_INPUT_FILE,     // no input file provided
        ERR_INVALID_FLAG,      // unrecognized flag
        ERR_INVALID_FILE_TYPE, // not a .quil/.qil file
        ERR_CANNOT_OPEN_FILE,  // failed to open the input file
        ERR_NO_OUTPUT_FILE,    // -o flag without an output name
        ERR_NO_EMIT_VALUE,     // --emit flag without a value
        ERR_INVALID_EMIT,      // unknown --emit type (expected ssa, asm, binary)
        ERR_NO_TARGET_VALUE,   // --target flag without a value
        ERR_INVALID_TARGET,    // unknown --target (expected a feather target name)
        ERR_INVALID_OPTLEVEL,  // bad -O level (expected 0 or 1)
        ERR_EMIT_UNSUPPORTED,  // valid --emit type the backend cannot produce yet
        ERR_NO_COMPILER,       // no C compiler found
        ERR_COMPILE_FAILED,    // C compiler returned a non-zero exit code
        ERR_RUNTIME_MISSING,   // installed runtime library not found

        // Updater errors
        ERR_UPDATE_START,             // could not start the update check
        ERR_UPDATE_CHECK,             // network/server error while checking
        ERR_UPDATE_TMP_DIR,           // could not create a temp directory
        ERR_UPDATE_DOWNLOAD_SCRIPT,   // failed to download install.sh
        ERR_UPDATE_DOWNLOAD_CHECKSUM, // failed to download the checksum
        ERR_UPDATE_CHECKSUM_VERIFY,   // checksum verification failed
        ERR_UPDATE_CHDIR,             // chdir failed
        ERR_UPDATE_EXEC,              // exec failed

        // Internal errors
        ERR_ALLOC_FAILED, // memory allocation failed
        ERR_UNKNOWN       // catch-all
} ErrorCode;

/*
 * @enum WarningCode
 * @brief All possible warnings the language can report.
 */
typedef enum {
        WARN_UNUSED_VARIABLE,     // declared but never used
        WARN_UNUSED_FUNCTION,     // defined but never called
        WARN_UNUSED_PARAMETER,    // parameter never used
        WARN_IMPLICIT_CONVERSION, // implicit numeric conversion
        WARN_MISSING_RETURN,      // non-void function without a return
        WARN_UNREACHABLE_CODE,    // code after return/break/continue
        WARN_SHADOWED_VARIABLE,   // inner scope shadows an outer variable
        WARN_DEPRECATED_FEATURE,  // usage of a deprecated feature
        WARN_UNKNOWN              // catch-all
} WarningCode;

/*
 * @brief Tells the error reporter which source file errors refer to.
 *
 * Must be called before any located error so messages can show the
 * "file:line:col" prefix. Passing NULL (default) disables file names.
 */
void error_set_source_file(const char *filename);

/*
 * @brief Reports an error (red) in the standard format and exits.
 *
 * @param stage The pipeline stage that reported the error.
 * @param code The error code describing the problem.
 * @param detail Extra information (like a token value), may be NULL.
 */
noreturn void quil_error(ErrorStage stage, ErrorCode code, const char *detail);

/*
 * @brief Same as quil_error but includes the source location.
 *
 * @param line 1-based line of the error (0 = no location).
 * @param col 1-based column of the error (0 = no location).
 */
noreturn void quil_error_at(ErrorStage stage, ErrorCode code, int line, int col, const char *detail);

/*
 * @brief Reports an "expected X but found Y" error and exits.
 *
 * Used by the parser's consume() for missing/expected tokens.
 *
 * @param stage The pipeline stage that reported the error.
 * @param expected Description of what was expected (e.g. "')'").
 * @param found The token value that was found instead, may be NULL.
 */
noreturn void quil_expected(ErrorStage stage, const char *expected, const char *found);

/*
 * @brief Same as quil_expected but includes the source location.
 *
 * @param line 1-based line of the error (0 = no location).
 * @param col 1-based column of the error (0 = no location).
 */
noreturn void quil_expected_at(ErrorStage stage, int line, int col, const char *expected, const char *found);

/*
 * @brief Reports a warning (yellow) in the standard format.
 *
 * Warnings do not exit the program.
 *
 * @param stage The pipeline stage that reported the warning.
 * @param code The warning code describing the problem.
 * @param detail Extra information, may be NULL.
 */
void quil_warning(ErrorStage stage, WarningCode code, const char *detail);

#endif

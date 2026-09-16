/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

#include "../include/error.h"
#include <unistd.h> // for _exit

// ANSI color codes used by the message formatter
#define ANSI_BOLD "\033[1;40m"   // bold program name
#define ANSI_RED "\033[1;31m"    // errors
#define ANSI_YELLOW "\033[1;33m" // warnings
#define ANSI_RESET "\033[0m"

// the source file errors refer to, set by error_set_source_file()
static const char *g_source_file = NULL;

void error_set_source_file(const char *filename) {
        g_source_file = filename;
}

// maps a stage to its display name
static const char *stage_name(ErrorStage stage) {
        switch (stage) {
        case STAGE_LEXER:
                return "lexer";
        case STAGE_PARSER:
                return "parser";
        case STAGE_SEMANTIC:
                return "semantic";
        case STAGE_CODEGEN:
                return "codegen";
        case STAGE_UPDATER:
                return "updater";
        case STAGE_FILE:
                return "file";
        case STAGE_INTERNAL:
                return "internal";
        default:
                return "unknown";
        }
}

// prints the shared prefix: "quil: <stage>: error:" / "quil: <stage>: warning:"
static void print_prefix(ErrorStage stage, const char *color, const char *kind) {
        fprintf(stderr, ANSI_BOLD "quil:" ANSI_RESET " ");
        fprintf(stderr, "%s%s: %s:" ANSI_RESET " ", color, stage_name(stage), kind);
}

// prints the "file:line:col:" location, when one is available
static void print_location(int line, int col) {
        if (line > 0 && col > 0 && g_source_file != NULL) {
                fprintf(stderr, "%s:%d:%d: ", g_source_file, line, col);
        }
}

// echoes the offending source line and points a caret at the error column.
// the line is read fresh from the source file, so it works after the lexer
// has already consumed the stream.
static void print_caret(int line, int col) {
        if (line <= 0 || col <= 0 || g_source_file == NULL) {
                return;
        }

        FILE *file = fopen(g_source_file, "r");
        if (file == NULL) {
                return;
        }

        // skip to the offending line
        int current = 1;
        int ch;
        while (current < line && (ch = fgetc(file)) != EOF) {
                if (ch == '\n') {
                        current++;
                }
        }
        if (current != line) {
                fclose(file);
                return;
        }

        // echo the line (tabs are shown as a single space so the caret aligns)
        fprintf(stderr, "    ");
        while ((ch = fgetc(file)) != EOF && ch != '\n') {
                fputc(ch == '\t' ? ' ' : ch, stderr);
        }
        fclose(file);

        fprintf(stderr, "\n    ");
        for (int i = 1; i < col; i++) {
                fputc(' ', stderr);
        }
        fprintf(stderr, "^\n");
}

// prints an error message (without the trailing newline)
static void print_error_message(ErrorStage stage, ErrorCode code, int line, int col, const char *detail) {
        const char *d = detail ? detail : "unknown";

        print_prefix(stage, ANSI_RED, "error");
        print_location(line, col);

        switch (code) {
        case ERR_UNEXPECTED_TOKEN:
                fprintf(stderr, "unexpected token '%s'", d);
                break;
        case ERR_EXPECTED_TYPE:
                fprintf(stderr, "expected a data type (int, float, etc.) but found '%s'", d);
                break;
        case ERR_EXPECTED_ID:
                fprintf(stderr, "expected an identifier but found '%s'", d);
                break;
        case ERR_MISSING_SEMICOLON:
                fprintf(stderr, "missing ';' after statement, found '%s'", d);
                break;
        case ERR_MISSING_PAREN:
                fprintf(stderr, "missing closing parenthesis, found '%s'", d);
                break;
        case ERR_INVALID_MODIFIER:
                fprintf(stderr, "invalid modifier '%s'", d);
                break;
        case ERR_INVALID_CALL_TARGET:
                fprintf(stderr, "can only call functions, found '%s'", d);
                break;
        case ERR_INVALID_ASSIGN_TARGET:
                fprintf(stderr, "invalid assignment target '%s'", d);
                break;
        case ERR_MISSING_END_OF_STATEMENT:
                fprintf(stderr, "expected ';' or newline after statement, found '%s'", d);
                break;
        case ERR_UNDECLARED_VAR:
                fprintf(stderr, "use of undeclared variable '%s'", d);
                break;
        case ERR_UNDECLARED_TYPE:
                fprintf(stderr, "use of undeclared type '%s'", d);
                break;
        case ERR_REDECLARED_VAR:
                fprintf(stderr, "variable '%s' already declared in this scope", d);
                break;
        case ERR_UNDECLARED_FUNC:
                fprintf(stderr, "call to undeclared function '%s'", d);
                break;
        case ERR_ARG_COUNT:
                fprintf(stderr, "%s", d);
                break;
        case ERR_DUPLICATED_FUNC:
                fprintf(stderr, "function '%s' is already defined", d);
                break;
        case ERR_DUPLICATED_TYPE:
                fprintf(stderr, "struct '%s' is already defined", d);
                break;
        case ERR_NO_MAIN:
                fprintf(stderr, "no 'fn main()' entry point defined");
                break;
        case ERR_MAIN_NOT_PUBLIC:
                fprintf(stderr, "main must be declared as 'public fn main()'");
                break;
        case ERR_TOP_LEVEL_STMT:
                fprintf(stderr, "statement not allowed at top level; put it inside 'fn main()'");
                break;
        case ERR_NO_INPUT_FILE:
                fprintf(stderr, "no input file provided");
                break;
        case ERR_INVALID_FLAG:
                fprintf(stderr, "invalid flag '%s'", d);
                break;
        case ERR_INVALID_FILE_TYPE:
                fprintf(stderr, "file type not valid (expected '.quil' or '.qil')");
                break;
        case ERR_CANNOT_OPEN_FILE:
                fprintf(stderr, "could not open file '%s'", d);
                break;
        case ERR_NO_OUTPUT_FILE:
                fprintf(stderr, "output flag requires a file name after it");
                break;
        case ERR_NO_EMIT_VALUE:
                fprintf(stderr, "--emit requires a value (expected 'ssa', 'asm' or 'binary')");
                break;
        case ERR_INVALID_EMIT:
                fprintf(stderr, "invalid emit type '%s' (expected 'ssa', 'asm' or 'binary')", d);
                break;
        case ERR_NO_TARGET_VALUE:
                fprintf(stderr, "--target requires a value (expected a feather target name)");
                break;
        case ERR_INVALID_TARGET:
                fprintf(stderr, "invalid target '%s' (expected 'amd64_sysv', 'amd64_apple', 'amd64_win', 'arm64', 'arm64_apple' or 'rv64')", d);
                break;
        case ERR_INVALID_OPTLEVEL:
                fprintf(stderr, "invalid optimization level '%s' (expected 0 or 1)", d);
                break;
        case ERR_EMIT_UNSUPPORTED:
                fprintf(stderr, "emitting '%s' is not supported yet", d);
                break;
        case ERR_NO_COMPILER:
                fprintf(stderr, "no C compiler found (tried cc, gcc, clang, tcc)");
                break;
        case ERR_COMPILE_FAILED:
                fprintf(stderr, "C compiler failed on '%s'", d);
                break;
        case ERR_RUNTIME_MISSING:
                fprintf(stderr, "runtime not found at '%s' - run 'quil --repair' (or 'quil -r') to restore it", d);
                break;
        case ERR_UPDATE_START:
                fprintf(stderr, "could not start the update check");
                break;
        case ERR_UPDATE_CHECK:
                fprintf(stderr, "could not check for updates (network or server error)");
                break;
        case ERR_UPDATE_TMP_DIR:
                fprintf(stderr, "failed to create a temporary directory");
                break;
        case ERR_UPDATE_DOWNLOAD_SCRIPT:
                fprintf(stderr, "failed to download install.sh, try again");
                break;
        case ERR_UPDATE_DOWNLOAD_CHECKSUM:
                fprintf(stderr, "failed to download the checksum file, try again");
                break;
        case ERR_UPDATE_CHECKSUM_VERIFY:
                fprintf(stderr, "checksum verification failed");
                break;
        case ERR_UPDATE_CHDIR:
                fprintf(stderr, "chdir failed: %s", d);
                break;
        case ERR_UPDATE_EXEC:
                fprintf(stderr, "exec failed: %s", d);
                break;
        case ERR_ALLOC_FAILED:
                fprintf(stderr, "memory allocation failed");
                break;
        case ERR_UNKNOWN:
        default:
                fprintf(stderr, "%s", d);
                break;
        }
}

noreturn void quil_error_at(ErrorStage stage, ErrorCode code, int line, int col, const char *detail) {
        print_error_message(stage, code, line, col, detail);
        fprintf(stderr, "\n");
        print_caret(line, col);
        _exit(EXIT_FAILURE);
}

noreturn void quil_error(ErrorStage stage, ErrorCode code, const char *detail) {
        quil_error_at(stage, code, 0, 0, detail);
}

noreturn void quil_expected_at(ErrorStage stage, int line, int col, const char *expected, const char *found) {
        const char *f = found ? found : "unknown";

        print_prefix(stage, ANSI_RED, "error");
        print_location(line, col);
        fprintf(stderr, "expected %s but found '%s'\n", expected, f);
        print_caret(line, col);
        _exit(EXIT_FAILURE);
}

noreturn void quil_expected(ErrorStage stage, const char *expected, const char *found) {
        quil_expected_at(stage, 0, 0, expected, found);
}

void quil_warning(ErrorStage stage, WarningCode code, const char *detail) {
        const char *d = detail ? detail : "unknown";

        print_prefix(stage, ANSI_YELLOW, "warning");

        switch (code) {
        case WARN_UNUSED_VARIABLE:
                fprintf(stderr, "variable '%s' is declared but never used\n", d);
                break;
        case WARN_UNUSED_FUNCTION:
                fprintf(stderr, "function '%s' is defined but never called\n", d);
                break;
        case WARN_UNUSED_PARAMETER:
                fprintf(stderr, "parameter '%s' is never used\n", d);
                break;
        case WARN_IMPLICIT_CONVERSION:
                fprintf(stderr, "implicit conversion of '%s' may lose precision\n", d);
                break;
        case WARN_MISSING_RETURN:
                fprintf(stderr, "function '%s' has no return statement on all paths\n", d);
                break;
        case WARN_UNREACHABLE_CODE:
                fprintf(stderr, "unreachable code: %s\n", d);
                break;
        case WARN_SHADOWED_VARIABLE:
                fprintf(stderr, "variable '%s' shadows an outer declaration\n", d);
                break;
        case WARN_DEPRECATED_FEATURE:
                fprintf(stderr, "deprecated feature: %s\n", d);
                break;
        case WARN_UNKNOWN:
        default:
                fprintf(stderr, "an unknown warning occurred\n");
                break;
        }
}

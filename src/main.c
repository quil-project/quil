/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

/*
 * src/main.c
 * Main entry, Contains the pipeline
 */

#include "../include/ast.h"
#include "../include/cli.h"
#include "../include/error.h"
#include "../include/lexer.h"
#include "../include/mode.h"
#include "../include/parser.h"
#include "../include/sema.h"
#include "../include/ssagen.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * PIPLELINE: check if the files provided are right -> tokenize line by line ->
 *            put tokens in a single line in a token_list -> pass the token_list to parser ->
 *            turn the list into a ASTnode (syntax tree) -> pass the ast to parser to be parsed ->
 *            check the codegen mode (engine or normal) -> pass the parsed nodes (normal codegen or engine codegen) to be compiled into C ->
 *            generate payload.c -> compile payload.asm to payload (linux)
 *
 *            file.quil (input) -> src/main.c -> src/lexer.c src/lexer_filter.c src/helper.c -> src/main.c -> src/parser.c src/helper.c ->
 *            src/ast.c -> src/parser.c -> src/sema.c -> src/main.c -> src/ssagen = payload/payload.bin (output)
 *
 *            The CLI (flag parsing, --help/--version/--update) lives in src/cli.c.
 */

// - Compiling -
// returns an absolute path to an available C compiler, or NULL
static const char *find_compiler(void);
static void compile_to(const char *compiler, const char *c_path, const char *bin_path);

// --- MAIN ---
int main(int argc, char *argv[]) {
        cli_options opts;
        cli_parse(argc, argv, &opts);

        // --- NON-RUN ACTIONS (handled entirely by the CLI) ---
        switch (opts.action) {
        case CLI_ACTION_HELP:
                cli_print_help();
                return EXIT_SUCCESS;
        case CLI_ACTION_VERSION:
                cli_print_version();
                return EXIT_SUCCESS;
        case CLI_ACTION_UPDATE:
                // the update flag handles its own output and messaging
                return cli_update();
        case CLI_ACTION_REPAIR:
                // reinstalls the missing .quil-lang directory
                return cli_repair();
        case CLI_ACTION_RUN:
                break; // fall through to the pipeline
        }

        // --- BACKEND OPTIONS ---
        // selects the feather target (NULL = host default) and optimization level
        ssagen_apply_options(opts.target, opts.optlevel);

        // --- FILE HANDLING ---
        const char *filename = opts.filename;
        const char *extention = strrchr(filename, '.');
        FILE *buffer;

        // Checking if the file extention is valid or not.
        if (extention == NULL) {
                quil_error(STAGE_FILE, ERR_INVALID_FILE_TYPE, NULL);
        } else if (strcmp(extention, ".quil") == 0 || strcmp(extention, ".qil") == 0) {
                // checking if the file can be opened or not
                if ((buffer = fopen(filename, "r")) == NULL) {
                        quil_error(STAGE_FILE, ERR_CANNOT_OPEN_FILE, filename);
                }
                // tell the error reporter which file compile errors refer to
                error_set_source_file(filename);
                // If the file open is succesful it will continue with rest of the program.
        } else {
                quil_error(STAGE_FILE, ERR_INVALID_FILE_TYPE, NULL);
        }
        // ---------------------

        // --- MAIN ---
        // Running the loop till we hit EOF (End Of File).
        token_list list;
        token_list_init(&list);
        token tokens = lexer_tokenizer(buffer);
        while (tokens.type != TOKEN_EOF) {
                token_list_add(&list, tokens);
                if (opts.debug_lexer) {
                        // NOTE: this function call is for debugging purposes.
                        lexer_print_token(tokens);
                }
                // Update tokens for the next iteration
                tokens = lexer_tokenizer(buffer);
        }
        token_list_add(&list, tokens);

        // checking program mode if ENGINE_MODE is not enabled
        if (!ENGINE_MODE) check_program_mode(&list);

        ASTnode *ast = quil_parse(&list);
        if (opts.debug_ast) {
                // NOTE: this function call is for debugging purposes.
                print_ast(ast, 0);
        }

        // --- SEMANTIC ANALYSIS ---
        // catches undeclared/redeclared variables before codegen
        semantic_analyze(ast);

        // --- CODE GENERATION (single backend: ssagen -> feather -> asm) ---
        // codegen_c kept on disk for reference but excluded from pipeline
        bool is_ssa = (opts.emit == CLI_EMIT_SSA);
        bool keep_asm = (opts.emit == CLI_EMIT_ASM) || is_ssa;
        char asm_buf[1024];
        char ext[8];
        if (is_ssa) strcpy(ext, ".ssa");
        else if (keep_asm) strcpy(ext, ".s");
        else strcpy(ext, ".tmp.s");
        char *asm_path = keep_asm ? "a.out.s" : "a.out.tmp.s";
        if (is_ssa && !keep_asm) asm_path = "a.out.ssa";
        if (opts.out_mode == CLI_OUT_BINARY) {
                snprintf(asm_buf, sizeof(asm_buf), "%s%s", opts.out_name, ext);
                asm_path = asm_buf;
        }
        if (is_ssa) strcpy(asm_buf, asm_path); // keep for later
        IlModule *mod = ssagen_build(ast);     // prog->statements include/ast.h:86 scope cur_ns src/sema.c:346
        FILE *f = fopen(asm_path, "w");
        if (!f) quil_error(STAGE_CODEGEN, ERR_CANNOT_OPEN_FILE, asm_path);
        if (is_ssa) ssagen_emit_ssa(mod, f);
        else ssagen_emit_asm(mod, f);
        fclose(f);

        free_ast_node(ast);
        // freeing the list and its tokens' values
        token_list_free(&list);

        // NOTE: the backend writes feather assembly to asm_path here (ssagen -> feather).
        if (keep_asm) {
                // --emit=asm: keep the .s file, skip assemble/link
                fclose(buffer);
                return EXIT_SUCCESS;
        }

        const char *compiler = find_compiler();
        // compile asm -> binary (feather emits assembly), then delete the temp .s
        compile_to(compiler, asm_path, opts.out_name);
        remove(asm_path);

        // NOTE: 2 if statement below are and for debugging purposes.
        /*
        if (ENGINE_MODE) {
                printf("Enabled.\n");
        } else {
                printf("Disabled.\n");
        }
        */
        // ------------

        // --- CLEAN UP AND FINALIZATION ---
        // closing the file buffer
        fclose(buffer);
        return EXIT_SUCCESS;
        // ---------------------------------
}

// --- Compiling ---
static const char *find_compiler(void) {
        // check for overriden custom CC veriable
        const char *env = getenv("CC");
        if (env && env[0] != '\0') {
                return env;
        }
        static char path[1027];
        const char *cands[] = {"cc", "gcc", "clang", "tcc", NULL};
        const char *paths = getenv("PATH"); // list of directories to search
        if (!paths) {
                return NULL;
        }
        char dir[1024];
        const char *p = paths; // keeping track of the current path
        while (*p) {
                const char *end = strchr(p, ':');
                size_t len = end ? (size_t)(end - p) : strlen(p);
                snprintf(dir, sizeof(dir), "%.*s", (int)len, p);
                for (int i = 0; cands[i]; i++) {
                        snprintf(path, sizeof(path), "%s/%s", dir, cands[i]);
                        if (access(path, X_OK) == 0) {
                                return path;
                        }
                }
                if (!end) {
                        break;
                }
                p = end + 1;
        }
        return NULL;
}
static void compile_to(const char *compiler, const char *c_path, const char *bin_path) {
        if (!compiler) {
                quil_error(STAGE_CODEGEN, ERR_NO_COMPILER, NULL);
        }

        char *args[6];
        args[0] = (char *)compiler;
        args[1] = "-O3";
        if (bin_path) {
                args[2] = "-o";
                args[3] = (char *)bin_path;
                args[4] = (char *)c_path;
                args[5] = NULL;
        } else {
                args[2] = (char *)c_path;
                args[3] = NULL;
        }
        if (run_cmd(NULL, compiler, args) != 0) {
                quil_error(STAGE_CODEGEN, ERR_COMPILE_FAILED, c_path);
        }
}

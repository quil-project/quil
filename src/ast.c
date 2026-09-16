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
 * @file ast.c
 * @brief Implementation of Abstract Syntax Tree (AST) nodes and management.
 *
 * This file contains functions for creating, managing, and destroying AST nodes.
 * The AST is the central data structure used by the parser to represent the
 * source code structure and by the codegen to generate target code.
 *
 * Memory Management:
 * - Nodes are allocated on the heap using create_ast_node.
 * - Nodes with collections (like PROGRAM or BLOCK) use dynamic arrays of pointers.
 * - free_ast_node recursively frees the entire tree to prevent leaks.
 */

#include "../include/ast.h"

/*
 * @brief Allocates and initializes a new AST node of a given type.
 *
 * This is the base allocation function. It zeros out the memory and sets the type.
 * All specialized factory functions call this.
 *
 * @param type The nodeType to assign to the new node.
 * @return ASTnode* Pointer to the newly allocated node.
 */
ASTnode *create_ast_node(nodeType type) {
        ASTnode *node = (ASTnode *)malloc(sizeof(ASTnode));
        if (node == NULL) {
                quil_error(STAGE_INTERNAL, ERR_ALLOC_FAILED, NULL);
        }
        memset(node, 0, sizeof(ASTnode));
        node->type = type;
        return node;
}

// returns a human-readable name for a node type, for error messages
const char *node_type_name(nodeType type) {
        switch (type) {
        case NODE_PROGRAM:
                return "program";
        case NODE_VAR_DECL:
                return "variable declaration";
        case NODE_ASSIGN:
                return "assignment";
        case NODE_FUNC_CALL:
                return "function call";
        case NODE_FUNC_DEF:
                return "function definition";
        case NODE_MEMBER_ACCESS:
                return "member access";
        case NODE_ARRAY_ACCESS:
                return "array access";
        case NODE_INT_LITERAL:
                return "number literal";
        case NODE_FLOAT_LITERAL:
                return "float literal";
        case NODE_STRING_LITERAL:
                return "string literal";
        case NODE_BOOL_LITERAL:
                return "bool literal";
        case NODE_CHAR_LITERAL:
                return "char literal";
        case NODE_IDENTIFIER:
                return "identifier";
        case NODE_BINARY_EXPRESSION:
                return "binary expression";
        case NODE_UNARY_EXPRESSION:
                return "unary expression";
        case NODE_TERNARY_EXPRESSION:
                return "ternary expression";
        case NODE_BLOCK:
                return "block";
        case NODE_IF_STAT:
                return "if statement";
        case NODE_WHILE:
                return "while loop";
        case NODE_FOR:
                return "for loop";
        case NODE_IMPORT:
                return "import";
        case NODE_DIRECTIVE:
                return "directive";
        case NODE_RETURN:
                return "return statement";
        case NODE_BREAK:
                return "break statement";
        case NODE_CONTINUE:
                return "continue statement";
        case NODE_LIST_LITERAL:
                return "vec literal";
        case NODE_NAMESPACE:
                return "namespace";
        case NODE_QUALIFIED:
                return "qualified path";
        case NODE_STRUCT_DEF:
                return "struct definition";
        default:
                return "expression";
        }
}

// --- Specialized Factory Functions ---
// These functions wrap create_ast_node and populate the type-specific union data.
ASTnode *make_int_node(int value) {
        ASTnode *node = create_ast_node(NODE_INT_LITERAL);
        node->data.int_literal.value = value;
        return node;
}
ASTnode *make_float_node(double value) {
        ASTnode *node = create_ast_node(NODE_FLOAT_LITERAL);
        node->data.float_literal.value = value;
        return node;
}
ASTnode *make_string_node(char *value) {
        ASTnode *node = create_ast_node(NODE_STRING_LITERAL);
        node->data.string_literal.value = strdup(value);
        return node;
}
ASTnode *make_bool_node(bool value) {
        ASTnode *node = create_ast_node(NODE_BOOL_LITERAL);
        node->data.bool_literal.value = value;
        return node;
}
ASTnode *make_char_node(char value) {
        ASTnode *node = create_ast_node(NODE_CHAR_LITERAL);
        node->data.char_literal.value = value;
        return node;
}
ASTnode *make_identifier_node(char *name) {
        ASTnode *node = create_ast_node(NODE_IDENTIFIER);
        node->data.identifier.name = strdup(name);
        return node;
}
ASTnode *make_binary_node(ASTnode *left, tokenType op, ASTnode *right) {
        ASTnode *node = create_ast_node(NODE_BINARY_EXPRESSION);
        node->data.binary_expression.left = left;
        node->data.binary_expression.op = op;
        node->data.binary_expression.right = right;
        return node;
}
ASTnode *make_unary_node(tokenType op, ASTnode *left) {
        ASTnode *node = create_ast_node(NODE_UNARY_EXPRESSION);
        node->data.unary_expression.op = op;
        node->data.unary_expression.left = left;
        return node;
}
ASTnode *make_ternary_node(ASTnode *condition, ASTnode *then_expr, ASTnode *else_expr) {
        ASTnode *node = create_ast_node(NODE_TERNARY_EXPRESSION);
        node->data.ternary_expression.condition = condition;
        node->data.ternary_expression.then_expr = then_expr;
        node->data.ternary_expression.else_expr = else_expr;
        return node;
}
ASTnode *make_if_stat_node(ASTnode *condition, ASTnode *then_block, ASTnode *else_block) {
        ASTnode *node = create_ast_node(NODE_IF_STAT);
        node->data.if_stat.condition = condition;
        node->data.if_stat.then_block = then_block;
        node->data.if_stat.else_block = else_block;
        return node;
}
ASTnode *make_while_node(ASTnode *condition, ASTnode *body) {
        ASTnode *node = create_ast_node(NODE_WHILE);
        node->data.while_loop.condition = condition;
        node->data.while_loop.body = body;
        return node;
}
ASTnode *make_for_node(ASTnode *init, ASTnode *condition, ASTnode *increment, ASTnode *body) {
        ASTnode *node = create_ast_node(NODE_FOR);
        node->data.for_loop.init = init;
        node->data.for_loop.condition = condition;
        node->data.for_loop.increment = increment;
        node->data.for_loop.body = body;
        return node;
}
ASTnode *make_var_decl_node(char *type_name, char *modifiers, char *name, ASTnode *value, bool is_array, int array_size) {
        ASTnode *node = create_ast_node(NODE_VAR_DECL);
        node->data.var_decl.type_name = type_name ? strdup(type_name) : NULL;
        node->data.var_decl.modifiers = modifiers ? strdup(modifiers) : NULL;
        node->data.var_decl.name = strdup(name);
        node->data.var_decl.value = value;
        node->data.var_decl.is_array = is_array;
        node->data.var_decl.array_size = array_size;
        return node;
}
ASTnode *make_assign_node(char *name, ASTnode *value) {
        ASTnode *node = create_ast_node(NODE_ASSIGN);
        node->data.assign.name = strdup(name);
        node->data.assign.value = value;
        node->data.assign.is_member = false;
        node->data.assign.obj = NULL;
        node->data.assign.member = NULL;
        node->data.assign.field_offset = 0;
        return node;
}
ASTnode *make_array_assign_node(char *name, ASTnode *index, ASTnode *value) {
        ASTnode *node = create_ast_node(NODE_ASSIGN);
        node->data.assign.name = strdup(name);
        node->data.assign.index = index;
        node->data.assign.value = value;
        node->data.assign.is_member = false;
        node->data.assign.obj = NULL;
        node->data.assign.member = NULL;
        node->data.assign.field_offset = 0;
        return node;
}
ASTnode *make_member_assign_node(ASTnode *obj, char *member, ASTnode *value) {
        ASTnode *node = create_ast_node(NODE_ASSIGN);
        node->data.assign.name = NULL;
        node->data.assign.value = value;
        node->data.assign.is_member = true;
        node->data.assign.obj = obj;
        node->data.assign.member = strdup(member);
        node->data.assign.field_offset = -1; // resolved by sema
        return node;
}
ASTnode *make_func_call_node(char *name, ASTnode **args, int arg_count) {
        ASTnode *node = create_ast_node(NODE_FUNC_CALL);
        node->data.func_call.name = strdup(name);
        node->data.func_call.args = args;
        node->data.func_call.arg_count = arg_count;
        return node;
}
ASTnode *make_func_def_node(char *return_type, char *name, ASTnode **params, int param_count, ASTnode *body) {
        ASTnode *node = create_ast_node(NODE_FUNC_DEF);
        node->data.func_def.return_type = return_type ? strdup(return_type) : NULL;
        node->data.func_def.name = strdup(name);
        node->data.func_def.params = params;
        node->data.func_def.param_count = param_count;
        node->data.func_def.body = body;
        node->data.func_def.is_public = false;
        node->data.func_def.is_extern = false;
        return node;
}
ASTnode *make_directive_node(char *name, char *value) {
        ASTnode *node = create_ast_node(NODE_DIRECTIVE);
        node->data.directive.name = strdup(name);
        node->data.directive.value = value ? strdup(value) : NULL;
        return node;
}
ASTnode *make_list_literal_node(ASTnode **elements, int count) {
        ASTnode *node = create_ast_node(NODE_LIST_LITERAL);
        node->data.list_literal.elements = elements;
        node->data.list_literal.count = count;
        node->data.list_literal.capacity = count;
        return node;
}
ASTnode *make_member_access_node(ASTnode *object, char *member, ASTnode **args, int arg_count) {
        ASTnode *node = create_ast_node(NODE_MEMBER_ACCESS);
        node->data.member_access.object = object;
        node->data.member_access.member = strdup(member);
        node->data.member_access.args = args;
        node->data.member_access.arg_count = arg_count;
        node->data.member_access.field_offset = -1; // resolved by sema for struct reads
        return node;
}
ASTnode *make_namespace_node(char *name, ASTnode *body) {
        ASTnode *node = create_ast_node(NODE_NAMESPACE);
        node->data.namespace_decl.name = strdup(name);
        node->data.namespace_decl.body = body;
        return node;
}
ASTnode *make_qualified_node(char **segments, int count) {
        ASTnode *node = create_ast_node(NODE_QUALIFIED);
        node->data.qualified.segments = segments;
        node->data.qualified.count = count;
        return node;
}
ASTnode *make_struct_def_node(char *name) {
        ASTnode *node = create_ast_node(NODE_STRUCT_DEF);
        node->data.struct_def.name = strdup(name);
        node->data.struct_def.fields = NULL;
        node->data.struct_def.field_count = 0;
        node->data.struct_def.field_capacity = 0;
        return node;
}
void ast_add_struct_field(ASTnode *struct_def, ASTnode *field) {
        if (struct_def->type != NODE_STRUCT_DEF) {
                return;
        }
        if (struct_def->data.struct_def.field_count >= struct_def->data.struct_def.field_capacity) {
                struct_def->data.struct_def.field_capacity = struct_def->data.struct_def.field_capacity == 0 ? 4 : struct_def->data.struct_def.field_capacity * 2;
                struct_def->data.struct_def.fields = (ASTnode **)realloc(struct_def->data.struct_def.fields,
                                                                          sizeof(ASTnode *) * (size_t)struct_def->data.struct_def.field_capacity);
        }
        struct_def->data.struct_def.fields[struct_def->data.struct_def.field_count++] = field;
}

/*
 * @brief sets the location for better error message
 */
void ast_set_loc(ASTnode *node, int line, int col) {
        node->line = line;
        node->col = col;
}

// --- Helper Functions for Collections ---
/*
 * @brief Adds a statement to a program or block node.
 *
 * This function manages a dynamic array (the "pool of pointers") for statements.
 * If the current capacity is reached, it doubles the size using realloc.
 *
 * @param parent The parent node (must be NODE_PROGRAM or NODE_BLOCK).
 * @param stmt The statement node to add.
 */
void ast_add_statement(ASTnode *parent, ASTnode *stmt) {
        if (parent->type == NODE_PROGRAM) {
                // If the dynamic array is full, grow it.
                if (parent->data.program.count >= parent->data.program.capacity) {
                        parent->data.program.capacity = parent->data.program.capacity == 0 ? 8 : parent->data.program.capacity * 2;
                        parent->data.program.statements = (ASTnode **)realloc(parent->data.program.statements, sizeof(ASTnode *) * parent->data.program.capacity);
                }
                // Add the pointer to the end of the array.
                parent->data.program.statements[parent->data.program.count++] = stmt;
        } else if (parent->type == NODE_BLOCK) {
                // Same logic for blocks.
                if (parent->data.blocks.count >= parent->data.blocks.capacity) {
                        parent->data.blocks.capacity = parent->data.blocks.capacity == 0 ? 8 : parent->data.blocks.capacity * 2;
                        parent->data.blocks.statements = (ASTnode **)realloc(parent->data.blocks.statements, sizeof(ASTnode *) * parent->data.blocks.capacity);
                }
                parent->data.blocks.statements[parent->data.blocks.count++] = stmt;
        }
}
/*
 * @brief Adds an argument to a function call node.
 */
void ast_add_arg(ASTnode *func_call, ASTnode *arg) {
        // verifying correct node type
        if (func_call->type != NODE_FUNC_CALL) {
                return;
        }
        // Simple realloc for every new argument. Consider using capacity logic if performance becomes an issue.
        func_call->data.func_call.args = (ASTnode **)realloc(func_call->data.func_call.args, sizeof(ASTnode *) * (func_call->data.func_call.arg_count + 1));
        func_call->data.func_call.args[func_call->data.func_call.arg_count++] = arg;
}
/*
 * @brief Adds an argument to a method-call's argument list (built before the member node exists).
 */
void ast_add_member_arg(ASTnode ***args, int *arg_count, ASTnode *arg) {
        *args = (ASTnode **)realloc(*args, sizeof(ASTnode *) * (*arg_count + 1));
        (*args)[(*arg_count)++] = arg;
}
/*
 * @brief Adds a parameter to a function definition node.
 */
void ast_add_param(ASTnode *func_def, ASTnode *param) {
        // verifying correct node type
        if (func_def->type != NODE_FUNC_DEF) {
                return;
        }
        func_def->data.func_def.params = (ASTnode **)realloc(func_def->data.func_def.params, sizeof(ASTnode *) * (func_def->data.func_def.param_count + 1));
        func_def->data.func_def.params[func_def->data.func_def.param_count++] = param;
}

// --- Memory Management ---
/*
 * @brief Recursively frees an AST node and all its children.
 *
 * This is crucial for preventing memory leaks. It uses a post-order traversal
 * pattern: it frees all child nodes (and their internal strings/arrays) before
 * finally freeing the node itself.
 *
 * @param node The root of the (sub)tree to free.
 */
void free_ast_node(ASTnode *node) {
        // Base case: NULL pointers are safe to "free".
        if (node == NULL) {
                return;
        }

        // Switch on type to handle specific child cleanup.
        switch (node->type) {
        case NODE_PROGRAM:
                // Free all statement nodes in the program.
                for (int i = 0; i < node->data.program.count; i++) {
                        free_ast_node(node->data.program.statements[i]);
                }
                // Free the array of pointers itself.
                free(node->data.program.statements);
                break;
        case NODE_VAR_DECL:
                free(node->data.var_decl.type_name);
                free(node->data.var_decl.modifiers);
                free(node->data.var_decl.name);
                free_ast_node(node->data.var_decl.value);
                break;
        case NODE_ASSIGN:
                free(node->data.assign.name);
                if (node->data.assign.index) free_ast_node(node->data.assign.index);
                if (node->data.assign.is_member) {
                        free_ast_node(node->data.assign.obj);
                        free(node->data.assign.member);
                }
                free_ast_node(node->data.assign.value);
                break;
        case NODE_FUNC_CALL:
                free(node->data.func_call.name);
                for (int i = 0; i < node->data.func_call.arg_count; i++) {
                        free_ast_node(node->data.func_call.args[i]);
                }
                free(node->data.func_call.args);
                break;
        case NODE_FUNC_DEF:
                free(node->data.func_def.return_type);
                free(node->data.func_def.name);
                for (int i = 0; i < node->data.func_def.param_count; i++) {
                        free_ast_node(node->data.func_def.params[i]);
                }
                free(node->data.func_def.params);
                free_ast_node(node->data.func_def.body);
                break;
        case NODE_MEMBER_ACCESS:
                free_ast_node(node->data.member_access.object);
                free(node->data.member_access.member);
                for (int i = 0; i < node->data.member_access.arg_count; i++) {
                        free_ast_node(node->data.member_access.args[i]);
                }
                free(node->data.member_access.args);
                break;
        case NODE_ARRAY_ACCESS:
                free(node->data.array_access.name);
                free_ast_node(node->data.array_access.index);
                break;
        case NODE_STRING_LITERAL:
                free(node->data.string_literal.value);
                break;
        case NODE_IDENTIFIER:
                free(node->data.identifier.name);
                break;
        case NODE_BINARY_EXPRESSION:
                free_ast_node(node->data.binary_expression.left);
                free_ast_node(node->data.binary_expression.right);
                break;
        case NODE_UNARY_EXPRESSION:
                free_ast_node(node->data.unary_expression.left);
                break;
        case NODE_TERNARY_EXPRESSION:
                free_ast_node(node->data.ternary_expression.condition);
                free_ast_node(node->data.ternary_expression.then_expr);
                free_ast_node(node->data.ternary_expression.else_expr);
                break;
        case NODE_BLOCK:
                for (int i = 0; i < node->data.blocks.count; i++) {
                        free_ast_node(node->data.blocks.statements[i]);
                }
                free(node->data.blocks.statements);
                break;
        case NODE_IF_STAT:
                free_ast_node(node->data.if_stat.condition);
                free_ast_node(node->data.if_stat.then_block);
                free_ast_node(node->data.if_stat.else_block);
                break;
        case NODE_WHILE:
                free_ast_node(node->data.while_loop.condition);
                free_ast_node(node->data.while_loop.body);
                break;
        case NODE_FOR:
                free_ast_node(node->data.for_loop.init);
                free_ast_node(node->data.for_loop.condition);
                free_ast_node(node->data.for_loop.increment);
                free_ast_node(node->data.for_loop.body);
                break;
        case NODE_IMPORT:
                free(node->data.import.lib_name);
                break;
        case NODE_DIRECTIVE:
                free(node->data.directive.name);
                free(node->data.directive.value);
                break;
        case NODE_RETURN:
                free_ast_node(node->data.returns.expression);
                break;
        case NODE_BREAK:
        case NODE_CONTINUE:
                break;
        case NODE_LIST_LITERAL:
                for (int i = 0; i < node->data.list_literal.count; i++) {
                        free_ast_node(node->data.list_literal.elements[i]);
                }
                free(node->data.list_literal.elements);
                break;
        case NODE_NAMESPACE:
                free(node->data.namespace_decl.name);
                free_ast_node(node->data.namespace_decl.body);
                break;
        case NODE_QUALIFIED:
                for (int i = 0; i < node->data.qualified.count; i++) free(node->data.qualified.segments[i]);
                free(node->data.qualified.segments);
                break;
        case NODE_STRUCT_DEF:
                free(node->data.struct_def.name);
                for (int i = 0; i < node->data.struct_def.field_count; i++) {
                        free_ast_node(node->data.struct_def.fields[i]);
                }
                free(node->data.struct_def.fields);
                break;
        default:
                break;
        }

        // Finally, free the node container itself.
        free(node->resolved_type);
        free(node);
}

// --- Debugging and Visualization ---

/*
 * @brief Prints a visual representation of the AST to stdout.
 *
 * This is a recursive function that uses indentation levels to show
 * the tree structure. Useful for debugging the parser.
 *
 * @param node The root node to start printing from.
 * @param level Current indentation level (starts at 0).
 */
// NOTE: This function is for debugging purposes.
void print_ast(ASTnode *node, int level) {
        if (node == NULL)
                return;

        // Print indentation based on the tree level.
        for (int i = 0; i < level; i++)
                printf("  ");

        switch (node->type) {
        case NODE_PROGRAM:
                printf("PROGRAM\n");
                for (int i = 0; i < node->data.program.count; i++) {
                        print_ast(node->data.program.statements[i], level + 1);
                }
                break;
        case NODE_VAR_DECL:
                printf("VAR_DECL: %s %s %s", node->data.var_decl.modifiers ? node->data.var_decl.modifiers : "",
                       node->data.var_decl.type_name ? node->data.var_decl.type_name : "", node->data.var_decl.name);
                if (node->data.var_decl.is_array) printf("[%d]", node->data.var_decl.array_size);
                printf("\n");
                if (node->data.var_decl.value) {
                        print_ast(node->data.var_decl.value, level + 1);
                }
                break;
        case NODE_ASSIGN:
                if (node->data.assign.is_member) {
                        printf("ASSIGN-MEMBER: %s\n", node->data.assign.member);
                        print_ast(node->data.assign.obj, level + 1);
                } else {
                        printf("ASSIGN: %s\n", node->data.assign.name);
                        if (node->data.assign.index) print_ast(node->data.assign.index, level + 1);
                }
                print_ast(node->data.assign.value, level + 1);
                break;
        case NODE_MEMBER_ACCESS:
                printf("MEMBER: %s (method: %s)\n", node->data.member_access.member,
                       node->data.member_access.arg_count > 0 ? "yes" : "no");
                print_ast(node->data.member_access.object, level + 1);
                for (int i = 0; i < node->data.member_access.arg_count; i++) {
                        print_ast(node->data.member_access.args[i], level + 1);
                }
                break;
        case NODE_INT_LITERAL:
                printf("INT: %d\n", node->data.int_literal.value);
                break;
        case NODE_FLOAT_LITERAL:
                printf("FLOAT: %f\n", node->data.float_literal.value);
                break;
        case NODE_IDENTIFIER:
                printf("IDENT: %s\n", node->data.identifier.name);
                break;
        case NODE_STRING_LITERAL:
                printf("STRING: %s\n", node->data.string_literal.value);
                break;
        case NODE_BINARY_EXPRESSION:
                printf("BINARY_OP: %s\n", lexer_token_type_to_string(node->data.binary_expression.op));
                print_ast(node->data.binary_expression.left, level + 1);
                print_ast(node->data.binary_expression.right, level + 1);
                break;
        case NODE_IF_STAT:
                printf("IF\n");
                print_ast(node->data.if_stat.condition, level + 1);
                printf("THEN\n");
                print_ast(node->data.if_stat.then_block, level + 1);
                if (node->data.if_stat.else_block) {
                        printf("ELSE\n");
                        print_ast(node->data.if_stat.else_block, level + 1);
                }
                break;
        case NODE_TERNARY_EXPRESSION:
                printf("TERNARY\n");
                printf("COND:\n");
                print_ast(node->data.ternary_expression.condition, level + 1);
                printf("THEN:\n");
                print_ast(node->data.ternary_expression.then_expr, level + 1);
                printf("ELSE:\n");
                print_ast(node->data.ternary_expression.else_expr, level + 1);
                break;
        case NODE_WHILE:
                printf("WHILE\n");
                print_ast(node->data.while_loop.condition, level + 1);
                printf("BODY\n");
                print_ast(node->data.while_loop.body, level + 1);
                break;
        case NODE_FOR:
                printf("FOR\n");
                printf("INIT:\n");
                print_ast(node->data.for_loop.init, level + 1);
                printf("COND:\n");
                print_ast(node->data.for_loop.condition, level + 1);
                printf("INC:\n");
                print_ast(node->data.for_loop.increment, level + 1);
                printf("BODY:\n");
                print_ast(node->data.for_loop.body, level + 1);
                break;
        case NODE_DIRECTIVE:
                printf("DIRECTIVE: @%s %s\n", node->data.directive.name, node->data.directive.value ? node->data.directive.value : "");
                break;
        case NODE_BREAK:
                printf("BREAK\n");
                break;
        case NODE_CONTINUE:
                printf("CONTINUE\n");
                break;
        case NODE_LIST_LITERAL:
                printf("LIST_LITERAL [%d elements]\n", node->data.list_literal.count);
                for (int i = 0; i < node->data.list_literal.count; i++) {
                        for (int j = 0; j < level + 1; j++)
                                printf("  ");
                        printf("[%d]: ", i);
                        print_ast(node->data.list_literal.elements[i], level + 1);
                }
                break;
        case NODE_NAMESPACE:
                printf("NAMESPACE: %s\n", node->data.namespace_decl.name);
                print_ast(node->data.namespace_decl.body, level + 1);
                break;
        case NODE_QUALIFIED:
                printf("QUALIFIED: ");
                for (int i = 0; i < node->data.qualified.count; i++) {
                        if (i) printf("::");
                        printf("%s", node->data.qualified.segments[i]);
                }
                printf("\n");
                break;
        case NODE_STRUCT_DEF:
                printf("STRUCT: %s [%d fields]\n", node->data.struct_def.name, node->data.struct_def.field_count);
                for (int i = 0; i < node->data.struct_def.field_count; i++) {
                        print_ast(node->data.struct_def.fields[i], level + 1);
                }
                break;
        default:
                printf("NODE_TYPE: %d\n", node->type);
                break;
        }
}

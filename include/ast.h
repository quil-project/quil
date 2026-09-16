/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

#ifndef AST_H
#define AST_H

#include "error.h"
#include "lexer.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * @enum nodeType
 * @brief Defines all possible types of nodes in the AST.
 */
typedef enum {
        NODE_PROGRAM,   // Root node; stores a list of top-level statements.
        NODE_VAR_DECL,  // Variable declaration (e.g., 'int x = 5').
        NODE_ASSIGN,    // Variable assignment (e.g., 'x = 10').
        NODE_FUNC_CALL, // Function invocation with arguments.
        NODE_FUNC_DEF,  // Function definition (parameters, return type, body).

        // Element Access
        NODE_MEMBER_ACCESS, // Accessing a struct/object member (e.g., 'obj.prop').
        NODE_ARRAY_ACCESS,  // Accessing an array element (e.g., 'arr[0]').

        // Literals and Identifiers
        NODE_INT_LITERAL,    // Integer constant (e.g., 42).
        NODE_FLOAT_LITERAL,  // Floating-point constant (e.g., 3.14).
        NODE_STRING_LITERAL, // String constant (e.g., "hello").
        NODE_BOOL_LITERAL,   // Boolean constant (true/false).
        NODE_CHAR_LITERAL,   // Character constant (e.g., 'a').
        NODE_IDENTIFIER,     // Variable or function name.

        // Expressions
        NODE_BINARY_EXPRESSION,  // Expression with two operands (e.g., a + b).
        NODE_UNARY_EXPRESSION,   // Expression with one operand (e.g., !a, -b).
        NODE_TERNARY_EXPRESSION, // NODE_TERNARY: cond ? then_expr : else_expr

        // Control Flow and Blocks
        NODE_BLOCK,   // A group of statements enclosed in braces {}.
        NODE_IF_STAT, // If-Else conditional statement.
        NODE_WHILE,   // While loop.
        NODE_FOR,     // For loop.

        // Built-in Statements
        NODE_IMPORT,    // Import/include another module.
        NODE_DIRECTIVE, // Directive (e.g., @import, @for)
        NODE_RETURN,    // Return from a function.
        NODE_BREAK,     // Break from a loop.
        NODE_CONTINUE,  // Continue to next loop iteration.
        NODE_LIST_LITERAL, // List literal (e.g., [1, 2, 3]).
        NODE_NAMESPACE, // Namespace/scope block e.g. `scope std { ... }` / `mod std { ... }`
        NODE_QUALIFIED, // Qualified path e.g. `std::println` (segments joined by ::)
        NODE_STRUCT_DEF // Struct definition e.g. `struct Point { int32 x \n int32 y \n }`
} nodeType;

// Forward declaration so the struct can reference itself
typedef struct ASTnode ASTnode;

/*
 * @struct ASTnode
 * @brief The fundamental building block of the AST.
 *
 * Each node has a type and a union containing data specific to that type.
 * This structure allows us to represent a wide variety of language constructs
 * using a single, unified type.
 */
struct ASTnode {
        nodeType type;       // What kind of node this is.
        int line;            // line of current token
        int col;             // column of current token
        char *resolved_type; // Set by semantic analysis; e.g. "int", "char *", "long int". NULL if not yet analyzed.

        union {
                // NODE_PROGRAM: The top-level entry point.
                struct {
                        ASTnode **statements; // Dynamic array of statement nodes.
                        int count;            // Number of statements.
                        int capacity;         // Allocated size of the array.
                } program;

                // NODE_VAR_DECL: Declaring a new variable.
                struct {
                        char *type_name; // e.g., "int", "float"
                        char *modifiers; // e.g., "unsigned", "long"
                        char *name;      // Variable name
                        ASTnode *value;  // Initial value expression (can be NULL)
                        bool is_array;
                        int array_size; // Size if it's an array, 0 otherwise
                } var_decl;

                // NODE_ASSIGN: Assigning a value to an existing variable.
                struct {
                        char *name;      // Variable name (NULL for member store)
                        ASTnode *index;  // Element index for arr[idx] = v, NULL for plain var
                        ASTnode *value;  // New value expression
                        bool is_member;  // true for obj.field = v
                        ASTnode *obj;    // member store target object (owned)
                        char *member;    // member store field name
                        int field_offset; // member store field offset (set by sema)
                } assign;

                // NODE_FUNC_CALL: Calling a function.
                struct {
                        char *name;     // Function name
                        ASTnode **args; // Dynamic array of argument expressions
                        int arg_count;  // Number of arguments
                } func_call;

                // NODE_FUNC_DEF: Defining a function.
                struct {
                        char *return_type;
                        char *name;
                        ASTnode **params; // Array of NODE_VAR_DECL nodes
                        int param_count;
                        ASTnode *body; // NODE_BLOCK containing the function code, NULL for extern prototype
                        bool is_public; // public fn -> exported
                        bool is_extern; // extern fn -> prototype, no body
                } func_def;

                // NODE_MEMBER_ACCESS: obj.member  (arg_count > 0 → method call obj.method(args))
                struct {
                        ASTnode *object;
                        char *member;
                        ASTnode **args; // NULL when property read
                        int arg_count;
                        int field_offset; // struct field offset (set by sema, -1 if unknown)
                } member_access;

                // NODE_ARRAY_ACCESS: arr[index]
                struct {
                        char *name;
                        ASTnode *index;
                } array_access;

                // NODE_INT_LITERAL
                struct {
                        int64_t value;
                } int_literal;

                // NODE_FLOAT_LITERAL
                struct {
                        double value;
                } float_literal;

                // NODE_STRING_LITERAL
                struct {
                        char *value;
                } string_literal;

                // NODE_BOOL_LITERAL
                struct {
                        bool value;
                } bool_literal;

                // NODE_CHAR_LITERAL
                struct {
                        char value;
                } char_literal;

                // NODE_IDENTIFIER
                struct {
                        char *name;
                } identifier;

                // NODE_BINARY_EXPRESSION: left OP right
                struct {
                        ASTnode *left;
                        tokenType op;
                        ASTnode *right;
                } binary_expression;

                // NODE_UNARY_EXPRESSION: OP left
                struct {
                        tokenType op;
                        ASTnode *left;
                } unary_expression;

                // NODE_TERNARY: cond ? then_expr : else_expr
                struct {
                        ASTnode *condition;
                        ASTnode *then_expr;
                        ASTnode *else_expr;
                } ternary_expression;

                // NODE_BLOCK: { statements... }
                struct {
                        ASTnode **statements; // Dynamic array of statement nodes
                        int count;
                        int capacity;
                } blocks;

                // NODE_IF_STAT
                struct {
                        ASTnode *condition;
                        ASTnode *then_block;
                        ASTnode *else_block; // Optional, can be NULL
                } if_stat;

                // NODE_WHILE
                struct {
                        ASTnode *condition;
                        ASTnode *body;
                } while_loop;

                // NODE_FOR
                struct {
                        ASTnode *init;      // Initialization statement
                        ASTnode *condition; // Loop condition
                        ASTnode *increment; // Increment expression
                        ASTnode *body;      // Loop body block
                } for_loop;

                // Built-in operations
                struct {
                        char *lib_name;
                } import;
                struct {
                        char *name;
                        char *value;
                } directive;
                struct {
                        ASTnode *expression;
                } returns;
                // NODE_LIST_LITERAL: [elem1, elem2, ...]
                struct {
                        ASTnode **elements;
                        int count;
                        int capacity;
                } list_literal;
                // NODE_NAMESPACE: scope std { ... }
                struct {
                        char *name;    // e.g. "std"
                        ASTnode *body; // NODE_BLOCK
                } namespace_decl;
                // NODE_QUALIFIED: std::io::println
                struct {
                        char **segments; // ["std","io","println"]
                        int count;
                } qualified;
                // NODE_STRUCT_DEF: struct Point { int32 x \n int32 y \n }
                struct {
                        char *name;          // e.g. "Point"
                        ASTnode **fields;    // array of NODE_VAR_DECL (type name, no init)
                        int field_count;
                        int field_capacity;
                } struct_def;
        } data;
};

// Creates a basic node with a specific type. Node initialization.
ASTnode *create_ast_node(nodeType type);
// specalized factory function for value assignition
ASTnode *make_int_node(int64_t value);
ASTnode *make_float_node(double value);
ASTnode *make_string_node(char *value);
ASTnode *make_bool_node(bool value);
ASTnode *make_char_node(char value);
ASTnode *make_identifier_node(char *name);
ASTnode *make_binary_node(ASTnode *left, tokenType op, ASTnode *right);
ASTnode *make_unary_node(tokenType op, ASTnode *left);
ASTnode *make_ternary_node(ASTnode *condition, ASTnode *then_expr, ASTnode *else_expr);
ASTnode *make_if_stat_node(ASTnode *condition, ASTnode *then_block, ASTnode *else_block);
ASTnode *make_while_node(ASTnode *condition, ASTnode *body);
ASTnode *make_for_node(ASTnode *init, ASTnode *condition, ASTnode *increment, ASTnode *body);
ASTnode *make_directive_node(char *name, char *value);
ASTnode *make_var_decl_node(char *type_name, char *modifiers, char *name, ASTnode *value, bool is_array, int array_size);
ASTnode *make_assign_node(char *name, ASTnode *value);
ASTnode *make_array_assign_node(char *name, ASTnode *index, ASTnode *value);
ASTnode *make_member_assign_node(ASTnode *obj, char *member, ASTnode *value);
ASTnode *make_func_call_node(char *name, ASTnode **args, int arg_count);
ASTnode *make_func_def_node(char *return_type, char *name, ASTnode **params, int param_count, ASTnode *body);
ASTnode *make_list_literal_node(ASTnode **elements, int count);
ASTnode *make_member_access_node(ASTnode *object, char *member, ASTnode **args, int arg_count);
ASTnode *make_namespace_node(char *name, ASTnode *body);
ASTnode *make_qualified_node(char **segments, int count);
ASTnode *make_struct_def_node(char *name);
void ast_add_struct_field(ASTnode *struct_def, ASTnode *field);
// sets the source location on a node for better error messages
void ast_set_loc(ASTnode *node, int line, int col);
// helper functions for collection
void ast_add_statement(ASTnode *parent, ASTnode *stmt);
void ast_add_arg(ASTnode *func_call, ASTnode *arg);
void ast_add_member_arg(ASTnode ***args, int *arg_count, ASTnode *arg);
void ast_add_param(ASTnode *func_def, ASTnode *param);
void ast_add_print_arg(ASTnode *print, ASTnode *arg);
// Memory management
void free_ast_node(ASTnode *node);
void print_ast(ASTnode *node, int level);
// returns a human-readable name for a node type, for error messages
const char *node_type_name(nodeType type);

#endif // !AST_H

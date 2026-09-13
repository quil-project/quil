/**************************************************
 * QUIL - Quick Unified Iterative Language
 * Language and Compiler toolchain, Frontend
 *
 * Copyright (c) 2026-present quil-project authors.
 * Licensed under the terms of the LICENSE file.
 *
 * Issues: <https://github.com/quil-project/quil>
 *************************************************/

// NOTE: Different filter functions in include/lexer.h will be defined here.
// These functions are used to filter unusual tokens and refine tokens.

#include "../include/lexer.h"
#include "../include/mode.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

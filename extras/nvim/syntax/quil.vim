"*************************************************
" QUIL - Quick Unified Iterative Language
" Language and Compiler toolchain, Frontend
"
" Copyright (c) 2026-present quil-project authors.
" Licensed under the terms of the LICENSE file.
"
" Issues: <https://github.com/quil-project/quil>
"*************************************************

" Vim syntax file
" Language: Quil
" Maintainer: tanvir-techbro

if exists("b:current_syntax")
  finish
endif

" 1. Directives & Imports (The @ stuff)
syntax match quilDirective "@\w\+"
syntax keyword quilLib stdlib math engine
highlight link quilDirective PreProc
highlight link quilLib Special

" 2. Keywords
syntax keyword quilKeyword if else while for break continue return fn scope public extern import
highlight link quilKeyword Statement

" 3. Data Types & Prefixes
syntax keyword quilType int8 int16 int32 int64 uint8 uint16 uint32 uint64 float32 float64 char string bool
highlight link quilType Type

" 5. Booleans
syntax keyword quilBool true false
highlight link quilBool Boolean

" 6. Numbers (Integers and Floats)
" Handles 42, 3.14, and .5
syntax match quilNumber "\b\d\+\(\.\d\+\)\?\b"
syntax match quilNumber "\.\d\+\b"
highlight link quilNumber Constant

" 7. Strings and Chars
syntax region quilString start='"' end='"' skip='\\"'
syntax region quilChar start="'" end="'" skip="\\'"
highlight link quilString String
highlight link quilChar Character

" 8. Operators and Symbols
" We group these so they don't look like plain text
syntax match quilOperator "[+=\-\*/%&^|!<>?~]"
syntax match quilDelimiter "[()\[\]{};:,.]"
highlight link quilOperator Operator
highlight link quilDelimiter Delimiter

" 9. Comments
syntax match quilComment "//.*$"
highlight link quilComment Comment

" 10. Special Escapes (the \n, \t stuff)
syntax match quilSpecial "\\n\|\\t\|\\0"
highlight link quilSpecial SpecialChar

let b:current_syntax = "quil"

#*************************************************
# QUIL - Quick Unified Iterative Language
# Language and Compiler toolchain, Frontend
#
# Copyright (c) 2026-present quil-project authors.
# Licensed under the terms of the LICENSE file.
#
# Issues: <https://github.com/quil-project/quil>
#*************************************************

# Variables
CC ?= cc
RCFLAGS += -Wall -Wextra -O2    # cflags for release make
DCFLAGS += -Wall -Wextra -g -O2 # cflags for default make
FEATHER_CFLAGS = -std=c99 -Wall -Wextra -Wpedantic -g -O2
SRC = src/main.c src/cli.c src/lexer/lexer.c src/lexer/filter.c src/lexer/helper.c \
      src/parser/parser.c src/parser/statement.c src/parser/declaration.c src/parser/expression.c \
      src/ast.c src/helper.c src/error.c src/_hashmap.c src/sema.c \
      src/codegen/ssagen.c src/codegen/types.c src/codegen/expr.c src/codegen/stmt.c
VERSION = $(shell cat VERSION)
BUILDDIR = build
OBJ = $(SRC:src/%.c=$(BUILDDIR)/src/%.o)
DEP = $(OBJ:.o=.d)
FEATHER_DEP = $(FEATHER_OBJ:.o=.d)

# Feather backend (mirrors feather/Makefile layout; paths are relative to repo root)
FEATHER_UTIL_SRC   = feather/src/util/util.c feather/src/util/parse.c
FEATHER_CORE_SRC   = feather/src/core/cfg.c feather/src/core/mem.c feather/src/core/ssa.c \
                     feather/src/core/alias.c feather/src/core/load.c feather/src/core/copy.c
FEATHER_OPT_SRC    = feather/src/opt/fold.c feather/src/opt/gvn.c feather/src/opt/gcm.c \
                     feather/src/opt/simpl.c feather/src/opt/ifopt.c
FEATHER_REG_SRC    = feather/src/reg/live.c feather/src/reg/spill.c feather/src/reg/rega.c
FEATHER_EMIT_SRC   = feather/src/emit/emit.c feather/src/emit/abi.c
FEATHER_AMD64_SRC  = feather/arch/amd64/targ.c feather/arch/amd64/sysv.c feather/arch/amd64/isel.c \
                     feather/arch/amd64/emit.c feather/arch/amd64/winabi.c
FEATHER_ARM64_SRC  = feather/arch/arm64/targ.c feather/arch/arm64/abi.c feather/arch/arm64/isel.c \
                     feather/arch/arm64/emit.c
FEATHER_RV64_SRC   = feather/arch/rv64/targ.c feather/arch/rv64/abi.c feather/arch/rv64/isel.c \
                     feather/arch/rv64/emit.c
FEATHER_FILAPI_SRC = feather/filapi/src/ilbuilder.c feather/filapi/src/data.c \
                     feather/filapi/src/module.c feather/filapi/src/type.c

# Include all feather sources (core lib, without feather/main.c)
# NOTE: no rv32 here — arch/rv32 has no .c files yet, wire it when targ.c lands
FEATHER_SRC = $(FEATHER_UTIL_SRC) $(FEATHER_CORE_SRC) $(FEATHER_OPT_SRC) \
              $(FEATHER_REG_SRC) $(FEATHER_EMIT_SRC) \
              $(FEATHER_AMD64_SRC) $(FEATHER_ARM64_SRC) $(FEATHER_RV64_SRC) \
              $(FEATHER_FILAPI_SRC)
FEATHER_OBJ = $(FEATHER_SRC:%.c=$(BUILDDIR)/%.o)

TARGET = bin/quil
MKDIR = mkdir -p $(BUILDDIR) bin
RM = rm -f

# Check for Termux
ifneq ($(wildcard /data/data/com.termux/files/usr/bin/*),)
    INSTALL_PATH ?= $(PREFIX)/bin
else
    INSTALL_PATH ?= /usr/local/bin
endif

# The default rule
all: $(TARGET)

# Per-file objects (incremental, parallel)
$(BUILDDIR)/src/%.o: src/%.c feather/config.h
	@mkdir -p $(dir $@)
	$(CC) $(DCFLAGS) -MMD -MP -c $< -o $@
# Generic rule for nested feather paths (mirrors feather's $(BUILDDIR)/%.o: %.c)
$(BUILDDIR)/%.o: %.c feather/config.h
	@mkdir -p $(dir $@)
	$(CC) $(FEATHER_CFLAGS) -MMD -MP -c $< -o $@

# Header deps (mirrors feather/Makefile; feather keeps identical copies at
# feather/*.h and feather/src/*.h, so depend on both)
$(FEATHER_UTIL_SRC:%.c=$(BUILDDIR)/%.o) $(FEATHER_CORE_SRC:%.c=$(BUILDDIR)/%.o) \
$(FEATHER_OPT_SRC:%.c=$(BUILDDIR)/%.o) $(FEATHER_REG_SRC:%.c=$(BUILDDIR)/%.o) \
$(FEATHER_EMIT_SRC:%.c=$(BUILDDIR)/%.o): feather/all.h feather/ops.h \
	feather/src/all.h feather/src/ops.h
$(FEATHER_FILAPI_SRC:%.c=$(BUILDDIR)/%.o): feather/filapi/include/ilbuilder.h \
	feather/filapi/include/data.h feather/filapi/include/module.h \
	feather/filapi/include/type.h feather/all.h feather/src/ops.h feather/config.h
$(FEATHER_AMD64_SRC:%.c=$(BUILDDIR)/%.o): feather/arch/amd64/all.h
$(FEATHER_ARM64_SRC:%.c=$(BUILDDIR)/%.o): feather/arch/arm64/all.h
$(FEATHER_RV64_SRC:%.c=$(BUILDDIR)/%.o): feather/arch/rv64/all.h

# feather/config.h picks the default target for the host (mirrors feather/Makefile)
feather/config.h:
	@case `uname` in                               \
	*Darwin*)                                      \
		case `uname -m` in                     \
		*arm64*)                               \
			echo "#define Deftgt T_arm64_apple";\
			;;                             \
		*)                                     \
			echo "#define Deftgt T_amd64_apple";\
			;;                             \
		esac                                   \
		;;                                     \
	*)                                             \
		case `uname -m` in                     \
		*aarch64*|*arm64*)                     \
			echo "#define Deftgt T_arm64"; \
			;;                             \
		*riscv64*)                             \
			echo "#define Deftgt T_rv64";  \
			;;                             \
		*)                                     \
			echo "#define Deftgt T_amd64_sysv";\
			;;                             \
		esac                                   \
		;;                                     \
	esac > $@

# Compile it to quil/bin/ directory
$(TARGET): $(OBJ) $(FEATHER_OBJ)
	@$(MKDIR)
	$(CC) $(DCFLAGS) $(OBJ) $(FEATHER_OBJ) -o $(TARGET)

# compiling without the -g flag so it has smaller binary
release: $(OBJ) $(FEATHER_OBJ)
	@$(MKDIR)
	$(CC) $(RCFLAGS) $(OBJ) $(FEATHER_OBJ) -o $(TARGET)

-include $(DEP)
-include $(FEATHER_DEP)

# VS Code extension (init the extras/vscode submodule, falling back to a plain clone)
vscode:
	@git submodule update --init --depth 1 extras/vscode 2>/dev/null || \
		git clone --depth 1 https://github.com/quil-project/quil-vscode.git extras/vscode
	@git -C extras/vscode fetch --depth 1 origin 2>/dev/null && git -C extras/vscode reset --hard origin/main 2>/dev/null || true

# Quil compiler backend (QBE fork) — pull the latest version, then build the feather binary
feather:
	@git submodule update --init --depth 1 feather 2>/dev/null || \
		git clone --depth 1 https://github.com/quil-project/feather.git feather
	@git -C feather fetch --depth 1 origin 2>/dev/null && git -C feather reset --hard origin/main 2>/dev/null || true
	$(MAKE) -C feather

# To install it locally
install: $(TARGET)
	mv $(TARGET) $(INSTALL_PATH)/

# Rule to clean up the binary
clean:
	$(RM) -r $(BUILDDIR) $(TARGET)

# Also drop the generated feather/config.h (mirrors feather's clean-gen)
clean-gen: clean
	$(RM) feather/config.h

# Neovim syntax activation
nvim:
	@$(MKDIR)
	chmod +x activate_syntax.sh && ./activate_syntax.sh

.PHONY: all release test clean clean-gen nvim install vscode feather

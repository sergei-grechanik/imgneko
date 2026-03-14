###############################################################################
# Basic paths
###############################################################################

# Absolute path to the repository root.
ROOT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# BUILD_DIR is normally passed by the generated wrapper Makefile.
# Keep a fallback for friendlier error messages when running from the root.
BUILD_DIR ?= $(ROOT_DIR)/build/default

# Important build-directory-local paths.
CONFIG_MK      := $(BUILD_DIR)/config.mk
WRAPPER_MKFILE := $(BUILD_DIR)/Makefile
BIN_DIR        := $(BUILD_DIR)/bin
OBJ_DIR        := $(BUILD_DIR)/obj
GEN_DIR        := $(BUILD_DIR)/generated

# When enabled, -MJ fragments are emitted alongside object files.
MJ_DIR         := $(BUILD_DIR)/compile_commands.d
COMPILE_DB     := $(BUILD_DIR)/compile_commands.json

# Final binary and generated metadata header.
TARGET       := $(BIN_DIR)/imgneko
BUILD_INFO_H := $(GEN_DIR)/build_info.h

# Checked-in version file.
VERSION_FILE := $(ROOT_DIR)/VERSION

###############################################################################
# Sources
###############################################################################

SOURCES := src/main.c

# Preserve the source tree under $(OBJ_DIR), so:
#   src/main.c -> $(OBJ_DIR)/src/main.o
OBJECTS := $(addprefix $(OBJ_DIR)/,$(SOURCES:.c=.o))
MJ_FRAGMENTS := $(addprefix $(MJ_DIR)/,$(SOURCES:.c=.json))

###############################################################################
# Fixed project metadata
###############################################################################

# VERSION must exist.
ifeq ($(wildcard $(VERSION_FILE)),)
$(error Missing required VERSION file)
endif

# Read the project version from the checked-in VERSION file.
IMGNEKO_VERSION := $(strip $(file <$(VERSION_FILE)))

###############################################################################
# Decide when config.mk must be included
###############################################################################

# These targets do not need config.mk:
#   - help: should always work
#   - clean: should remove outputs even if the build dir was never configured
NO_CONFIG_TARGETS := help clean

# Use the requested goals, or "all" if the user did not name one explicitly.
REQUESTED_GOALS := $(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)

# All other targets are normal build/install targets and require config.mk.
NEEDS_CONFIG := $(filter-out $(NO_CONFIG_TARGETS),$(REQUESTED_GOALS))

# Include the saved configuration only when needed.
#
# The generated config.mk should contain lines like:
#   override CC := clang
#   override CFLAGS := -O0 -g3 ...
#
# so the saved configuration becomes authoritative for normal builds.
ifneq ($(NEEDS_CONFIG),)
ifneq ($(wildcard $(CONFIG_MK)),)
include $(CONFIG_MK)
endif
endif

###############################################################################
# Derived settings
###############################################################################

# Convert the example feature toggle into a preprocessor define.
FEATURE_CPPFLAGS := $(if $(filter ON,$(FEATURE_X)),-DFEATURE_X=1,-DFEATURE_X=0)
ALL_OUTPUTS      := $(TARGET) $(if $(filter ON,$(COMP_DB_MJ)),$(COMPILE_DB))

# The generated header lives in $(GEN_DIR), so the compiler must search there.
BUILD_INFO_CPPFLAGS := -I$(GEN_DIR)

# Standard install location:
INSTALL_BINDIR := $(DESTDIR)$(PREFIX)/bin

# Escape values before embedding them into generated C string literals.
c_escape = $(subst ",\",$(subst \,\\,$(1)))

# Emit one #define line per saved configuration variable.
CONFIG_INFO_DEFINES := \
	$(foreach var,PROFILE PREFIX CC CPPFLAGS CFLAGS LDFLAGS LDLIBS FEATURE_X COMP_DB_MJ, \
		printf '%s\n' '#define BUILD_CONFIG_$(var) "$(call c_escape,$($(var)))"';)

###############################################################################
# Targets
###############################################################################

.DEFAULT_GOAL := all

.PHONY: all install clean help check-config-date compile_commands_json

# Normal build entry point.
all: check-config-date $(ALL_OUTPUTS)

compile_commands_json: $(COMPILE_DB)

# Check that config.mk exists and issue a warning if it is older than configure.
check-config-date:
	@if [ ! -f "$(CONFIG_MK)" ]; then \
		echo "error: $(CONFIG_MK) does not exist"; \
		echo "       run ./configure --build-dir='$(BUILD_DIR)' first"; \
		exit 1; \
	fi
	@if [ -f "$(ROOT_DIR)/configure" ]; then \
		if [ "$(CONFIG_MK)" -ot "$(ROOT_DIR)/configure" ]; then \
			echo "error: $(CONFIG_MK) is older than $(ROOT_DIR)/configure. Reconfigure or touch config.mk"; \
		fi \
	fi

###############################################################################
# Build rules
###############################################################################

# Link the final executable.
#
# config.mk and build_info.h are normal prerequisites so configuration changes
# or metadata changes force relinking.
#
# check-config-date is an order-only prerequisite so direct invocations like:
#   make /abs/path/to/build/debug/bin/imgneko
# still issue a warning if the saved profile is stale.
$(TARGET): $(OBJECTS) $(CONFIG_MK) $(BUILD_INFO_H) | check-config-date
	@mkdir -p "$(dir $@)"
	$(CC) $(LDFLAGS) -o "$@" $(OBJECTS) $(LDLIBS)

# Generate a header used by `--version` to print all build information.
$(BUILD_INFO_H): $(CONFIG_MK) $(VERSION_FILE) | check-config-date
	@mkdir -p "$(GEN_DIR)"
	@{ \
		compiled_at="$$(date -u '+%Y-%m-%d %H:%M:%S UTC')"; \
		printf '%s\n' '#ifndef BUILD_INFO_H'; \
		printf '%s\n' '#define BUILD_INFO_H'; \
		printf '%s\n' ''; \
		printf '%s\n' '// This file is generated, do not edit.'; \
		printf '%s\n' ''; \
		printf '%s\n' '#define BUILD_IMGNEKO_VERSION "$(call c_escape,$(IMGNEKO_VERSION))"'; \
		printf '%s\n' "#define BUILD_COMPILED_AT \"$$compiled_at\""; \
		$(CONFIG_INFO_DEFINES) \
		printf '%s\n' ''; \
		printf '%s\n' '#endif'; \
	} > "$@"

ifeq ($(COMP_DB_MJ),ON)
# When compile_commands.json is enabled, each compile emits its clang-style
# -MJ fragment alongside the object file in the same compiler invocation.
$(OBJ_DIR)/%.o: %.c $(CONFIG_MK) $(BUILD_INFO_H) | check-config-date
	@mkdir -p "$(dir $@)" "$(MJ_DIR)/$(dir $*)"
	$(CC) $(BUILD_INFO_CPPFLAGS) $(CPPFLAGS) $(FEATURE_CPPFLAGS) $(CFLAGS) -MJ "$(MJ_DIR)/$*.json" -c "$<" -o "$@"

$(COMPILE_DB): $(OBJECTS) | check-config-date
	@{ \
		printf '[\n'; \
		first=1; \
		for fragment in $(MJ_FRAGMENTS); do \
			if [ $$first -eq 0 ]; then printf ',\n'; fi; \
			sed '$$s/,[[:space:]]*$$//' "$$fragment"; \
			first=0; \
		done; \
		printf '\n]\n'; \
	} > "$@"
else
# Compile one source file into one object file.
#
# Objects depend on config.mk and build_info.h so that configuration changes and
# regenerated metadata trigger recompilation automatically.
$(OBJ_DIR)/%.o: %.c $(CONFIG_MK) $(BUILD_INFO_H) | check-config-date
	@mkdir -p "$(dir $@)"
	$(CC) $(BUILD_INFO_CPPFLAGS) $(CPPFLAGS) $(FEATURE_CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

$(COMPILE_DB): | check-config-date
	@echo "error: $(COMPILE_DB) requires ./configure --comp-db-mj" >&2
	@exit 1
endif

###############################################################################
# Utility targets
###############################################################################

# Install the built binary.
install: check-config-date $(TARGET)
	@mkdir -p "$(INSTALL_BINDIR)"
	install -m 0755 "$(TARGET)" "$(INSTALL_BINDIR)/imgneko"

# Remove build outputs but keep the saved configuration and wrapper Makefile.
clean:
	rm -rf "$(OBJ_DIR)" "$(BIN_DIR)" "$(GEN_DIR)" "$(MJ_DIR)" "$(COMPILE_DB)"

# Brief user-facing help.
help:
	@printf '%s\n' 'To build and install (build dir will be build/default):'
	@printf '%s\n' '  ./configure --prefix=/usr/local && make && make install'
	@printf '%s\n' ''
	@printf '%s\n' 'To build in a non-default build directory:'
	@printf '%s\n' '  ./configure --build-dir=build/mybuild'
	@printf '%s\n' '  make -C build/mybuild'
	@printf '%s\n' ''
	@printf '%s\n' 'To build with a non-default profile:'
	@printf '%s\n' '  ./configure --profile=debug'
	@printf '%s\n' '  make -C build/debug'
	@printf '%s\n' ''
	@printf '%s\n' 'To also generate compile_commands.json during normal builds:'
	@printf '%s\n' '  ./configure --profile=debug --comp-db-mj'
	@printf '%s\n' '  make -C build/debug'

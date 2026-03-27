###############################################################################
# Basic paths
###############################################################################

# Absolute path to the repository root.
ROOT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# BUILD_DIR is normally passed by the generated wrapper Makefile.
# Keep a fallback for root-level invocations that do not pass BUILD_DIR.
BUILD_DIR_IMPLICIT := $(if $(filter undefined,$(origin BUILD_DIR)),1,0)
ifeq ($(BUILD_DIR_IMPLICIT),1)
BUILD_DIR := $(ROOT_DIR)/build/default
endif

# Normalize user-supplied BUILD_DIR values so generated paths and test-runner
# environment variables stay absolute even when callers pass `build/foo/`.
override BUILD_DIR := $(abspath $(BUILD_DIR))

# Important build-directory-local paths.
CONFIG_MK      := $(BUILD_DIR)/config.mk
WRAPPER_MKFILE := $(BUILD_DIR)/Makefile
BIN_DIR        := $(BUILD_DIR)/bin
OBJ_DIR        := $(BUILD_DIR)/obj
GEN_DIR        := $(BUILD_DIR)/generated
TEST_BIN_DIR   := $(OBJ_DIR)/test-bin
TEST_OUTPUT_DIR := $(BUILD_DIR)/test-outputs

# Important targets.
BIN_IMGNEKO     := $(BIN_DIR)/imgneko
BIN_TEST_RUNNER := $(BIN_DIR)/test-runner
BUILD_INFO_H    := $(GEN_DIR)/build_info.h

# Additional targets.
COMPILE_DB      := $(BUILD_DIR)/compile_commands.json
STAGED_DEPFILE := $(BUILD_DIR)/dependencies.mk
FINAL_DEPFILE := $(ROOT_DIR)/mk/dependencies.mk

# Checked-in version file.
VERSION_FILE := $(ROOT_DIR)/VERSION

###############################################################################
# Sources
###############################################################################

APP_SOURCES := src/main.c
UTIL_SOURCES := $(shell if [ -d "$(ROOT_DIR)/src/util" ]; then cd "$(ROOT_DIR)" && find src/util -type f -name '*.c' -print | LC_ALL=C sort; fi)
TEST_RUNNER_SOURCE := testing/support/test-runner.c
TEST_SUPPORT_SOURCES := $(shell if [ -d "$(ROOT_DIR)/testing/support" ]; then cd "$(ROOT_DIR)" && find testing/support -type f -name '*.c' ! -name 'test-runner.c' -print | LC_ALL=C sort; fi)
TEST_C_SOURCES := $(shell if [ -d "$(ROOT_DIR)/testing/tests" ]; then cd "$(ROOT_DIR)" && find testing/tests -type f -name '*.c' -print | LC_ALL=C sort; fi)

# Preserve the source tree under $(OBJ_DIR), so:
#   src/main.c -> $(OBJ_DIR)/src/main.o
APP_OBJECTS := $(addprefix $(OBJ_DIR)/,$(APP_SOURCES:.c=.o))
UTIL_OBJECTS := $(addprefix $(OBJ_DIR)/,$(UTIL_SOURCES:.c=.o))
OBJECTS := $(APP_OBJECTS) $(UTIL_OBJECTS)
TEST_RUNNER_OBJECT := $(OBJ_DIR)/$(TEST_RUNNER_SOURCE:.c=.o)
TEST_SUPPORT_OBJECTS := $(addprefix $(OBJ_DIR)/,$(TEST_SUPPORT_SOURCES:.c=.o))
TEST_TOOLS := $(BIN_TEST_RUNNER)
TEST_C_BINS := $(patsubst testing/tests/%.c,$(TEST_BIN_DIR)/%.c.bin,$(TEST_C_SOURCES))
ALL_OBJECTS_AND_BINS := \
	$(OBJECTS) $(TEST_RUNNER_OBJECT) $(TEST_SUPPORT_OBJECTS) $(TEST_C_BINS)

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

# When BUILD_DIR was not set explicitly (for example plain `make` from the
# repository root), only auto-select build/default if there are no other
# configured build directories under ./build.
ifneq ($(NEEDS_CONFIG),)
ifeq ($(BUILD_DIR_IMPLICIT),1)
CONFIGURED_BUILD_DIRS := $(patsubst %/,%,$(sort $(dir $(wildcard $(ROOT_DIR)/build/*/config.mk))))
NONDEFAULT_CONFIGURED_BUILD_DIRS := $(filter-out $(ROOT_DIR)/build/default,$(CONFIGURED_BUILD_DIRS))
ifneq ($(NONDEFAULT_CONFIGURED_BUILD_DIRS),)
$(error error: found multiple configured build directories under $(ROOT_DIR)/build; run make -C build/<name> or pass BUILD_DIR=<path> explicitly)
endif
endif
endif

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

# Generated headers live in $(GEN_DIR), and internal headers live under src/.
COMMON_INCLUDES := -I$(GEN_DIR) -I$(ROOT_DIR)/src
TEST_SUPPORT_INCLUDES := -I$(ROOT_DIR)/testing/support
TEST_INCLUDES := $(COMMON_INCLUDES) $(TEST_SUPPORT_INCLUDES)

# Standard install location:
INSTALL_BINDIR := $(DESTDIR)$(PREFIX)/bin

# Escape values before embedding them into generated C string literals.
c_escape = $(subst ",\",$(subst \,\\,$(1)))

TEST_RUNNER_DEFINES := \
	-DTEST_RUNNER_ROOT_DIR=\"$(call c_escape,$(ROOT_DIR))\" \
	-DTEST_RUNNER_BUILD_DIR=\"$(call c_escape,$(BUILD_DIR))\"

# Helpers to convert *.bin and *.o targets to *.d depfile paths and *.json
# compile database fragment paths.
mj_fragment_for = $(patsubst %.bin,%.json,$(patsubst %.o,%.json,$(1)))
depfile_input_for = $(patsubst %.bin,%.d,$(patsubst %.o,%.d,$(1)))
GENERATED_DEPFILES := $(wildcard $(call depfile_input_for,$(ALL_OBJECTS_AND_BINS)))

# A command to combine the -MJ fragments into a complete compile_commands.json,
# or a no-op when the feature is disabled.
ifeq ($(COMP_DB_MJ),ON)
MJ_COMPILE_FLAGS = -MJ "$(call mj_fragment_for,$@)"
COMPILE_DB_REFRESH = "$(ROOT_DIR)/tools/build-compile-db.sh" "$(OBJ_DIR)" "$(COMPILE_DB)"
else
MJ_COMPILE_FLAGS =
COMPILE_DB_REFRESH = :
endif

# When explicitly enabled by configure, also emit per-target depfiles that can
# be normalized into the checked-in FINAL_DEPFILE.
ifeq ($(DEPFILES),ON)
DEPFILE_COMPILE_FLAGS = -MMD -MP -MT "$@" -MF "$(call depfile_input_for,$@)"
else
DEPFILE_COMPILE_FLAGS =
endif

# Flags that are common to every compile invocation.
COMMON_COMPILE_FLAGS = \
	$(CPPFLAGS) $(FEATURE_CPPFLAGS) $(CFLAGS) $(DEPFILE_COMPILE_FLAGS) \
	$(MJ_COMPILE_FLAGS)

# Emit one #define line per saved configuration variable.
CONFIG_INFO_DEFINES := \
	$(foreach var,PROFILE PREFIX CC CPPFLAGS CFLAGS LDFLAGS LDLIBS FEATURE_X COMP_DB_MJ DEPFILES, \
		printf '%s\n' '#define BUILD_CONFIG_$(var) "$(call c_escape,$($(var)))"';)

# Use the checked-in dependency file everywhere, then add any generated
# per-target depfiles that are already available in this build directory.
# Repeated rules are fine here: make merges prerequisites from all included
# depfiles as long as they do not define conflicting recipes.
ifneq ($(wildcard $(FINAL_DEPFILE)),)
include $(FINAL_DEPFILE)
endif
ifneq ($(GENERATED_DEPFILES),)
include $(GENERATED_DEPFILES)
endif

# Check that config.mk exists and fail if it is older than configure.
check-config-date:
	@if [ ! -f "$(CONFIG_MK)" ]; then \
		echo "error: $(CONFIG_MK) does not exist"; \
		echo "       run ./configure --build-dir='$(BUILD_DIR)' first"; \
		exit 1; \
	fi
	@if [ -f "$(ROOT_DIR)/configure" ]; then \
		if [ "$(CONFIG_MK)" -ot "$(ROOT_DIR)/configure" ]; then \
			echo "error: $(CONFIG_MK) is older than $(ROOT_DIR)/configure. Reconfigure or touch config.mk"; \
			exit 1; \
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
# still fail if the saved profile is stale.
$(BIN_IMGNEKO): $(OBJECTS) $(CONFIG_MK) $(BUILD_INFO_H) | check-config-date
	@mkdir -p "$(dir $@)"
	$(CC) $(LDFLAGS) -o "$@" $(OBJECTS) $(LDLIBS)

$(BIN_TEST_RUNNER): $(TEST_RUNNER_OBJECT) $(UTIL_OBJECTS) $(CONFIG_MK) $(BUILD_INFO_H) | check-config-date
	@mkdir -p "$(dir $@)"
	$(CC) $(LDFLAGS) -o "$@" $(TEST_RUNNER_OBJECT) $(UTIL_OBJECTS) $(LDLIBS)

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

$(TEST_RUNNER_OBJECT): $(TEST_RUNNER_SOURCE) $(CONFIG_MK) $(BUILD_INFO_H) | check-config-date
	@mkdir -p "$(dir $@)"
	$(CC) $(COMMON_INCLUDES) $(TEST_RUNNER_DEFINES) $(COMMON_COMPILE_FLAGS) -c "$<" -o "$@"

$(OBJ_DIR)/%.o: %.c $(CONFIG_MK) $(BUILD_INFO_H) | check-config-date
	@mkdir -p "$(dir $@)"
	$(CC) $(COMMON_INCLUDES) $(COMMON_COMPILE_FLAGS) -c "$<" -o "$@"

$(TEST_BIN_DIR)/%.c.bin: testing/tests/%.c $(TEST_SUPPORT_OBJECTS) $(UTIL_OBJECTS) $(CONFIG_MK) $(BUILD_INFO_H) | check-config-date
	@mkdir -p "$(dir $@)"
	$(CC) $(TEST_INCLUDES) $(COMMON_COMPILE_FLAGS) $(LDFLAGS) "$<" $(TEST_SUPPORT_OBJECTS) $(UTIL_OBJECTS) -o "$@" $(LDLIBS)

# Build a checked-in, normalized dependency file from per-target depfiles
# generated by an explicit depfile-enabled configure run.
ifeq ($(DEPFILES),ON)
$(STAGED_DEPFILE): $(ALL_OBJECTS_AND_BINS) $(ROOT_DIR)/tools/build-depfile.sh | check-config-date
	@"$(ROOT_DIR)/tools/build-depfile.sh" "$(ROOT_DIR)" "$(BUILD_DIR)" "$(OBJ_DIR)" "$@"

depfile: check-config-date $(ALL_OBJECTS_AND_BINS) $(ROOT_DIR)/tools/build-depfile.sh
	@"$(ROOT_DIR)/tools/build-depfile.sh" "$(ROOT_DIR)" "$(BUILD_DIR)" "$(OBJ_DIR)" "$(STAGED_DEPFILE)"
	@mkdir -p "$(dir $(FINAL_DEPFILE))"
	cp "$(STAGED_DEPFILE)" "$(FINAL_DEPFILE)"
else
depfile: check-config-date
	@echo "error: depfile generation is disabled in $(CONFIG_MK); rerun ./configure --build-dir='$(BUILD_DIR)' --depfiles"
	@exit 1
endif

###############################################################################
# Main targets
###############################################################################

.DEFAULT_GOAL := all

.PHONY: all install clean depfile help check-config-date test test-deps test-list test-tools test-c-bins clean-test-output

# Targets to build things.
all: check-config-date $(BIN_IMGNEKO)
	@$(COMPILE_DB_REFRESH)
test-tools: check-config-date $(TEST_TOOLS)
	@$(COMPILE_DB_REFRESH)
test-c-bins: check-config-date $(TEST_C_BINS)
	@$(COMPILE_DB_REFRESH)

# Build everything required to run tests without actually executing them.
test-deps: check-config-date all test-tools test-c-bins

# Install the built binary.
install: check-config-date all
	@mkdir -p "$(INSTALL_BINDIR)"
	install -m 0755 "$(BIN_IMGNEKO)" "$(INSTALL_BINDIR)/imgneko"

# Run tests.
test: check-config-date test-deps clean-test-output
	@set --; \
	if [ -n "$(FILTER)" ]; then set -- --filter "$(FILTER)"; else set -- --all; fi; \
	"$(BIN_TEST_RUNNER)" "$$@"

# List tests.
test-list: check-config-date test-tools test-c-bins
	@set -- --list; \
	if [ -n "$(FILTER)" ]; then set -- "$$@" --filter "$(FILTER)"; fi; \
	"$(BIN_TEST_RUNNER)" "$$@"

# Remove captured per-test output files so each `make test` run starts fresh.
clean-test-output: check-config-date
	rm -rf "$(TEST_OUTPUT_DIR)"

# Remove build outputs but keep the saved configuration and wrapper Makefile.
clean: clean-test-output
	rm -rf "$(OBJ_DIR)" "$(BIN_DIR)" "$(GEN_DIR)" "$(STAGED_DEPFILE)" "$(COMPILE_DB)"

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
	@printf '%s\n' ''
	@printf '%s\n' 'To regenerate the checked-in dependency file:'
	@printf '%s\n' '  ./configure --build-dir=build/depfiles --depfiles'
	@printf '%s\n' '  make -C build/depfiles depfile'
	@printf '%s\n' ''
	@printf '%s\n' 'To run tests from a configured build directory:'
	@printf '%s\n' '  make -C build/debug test'
	@printf '%s\n' '  make -C build/debug test FILTER='\''test-runner*|some_test.c/subtest'\'''
	@printf '%s\n' '  make -C build/debug test-list FILTER='\''*.sh|*.test'\'''

# SPDX-License-Identifier: MIT-0

###############################################################################
# Basic paths
###############################################################################

# Absolute path to the repository root.
ROOT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

# A helper to display paths relative to the current directory when possible.
display_path = $(if $(filter $(CURDIR),$(abspath $(1))),.,$(patsubst $(CURDIR)/%,./%,$(abspath $(1))))

# BUILD_DIR is normally passed by the generated wrapper Makefile. For direct
# root-level invocations without BUILD_DIR, reuse the only directory under
# ./build/ when that choice is unambiguous.
#
# When there is no usable implicit choice, store an obvious sentinel in
# BUILD_DIR itself. That keeps path expansion predictable for parse-time-only
# targets like `make help` without pretending that build/default is special.
BUILD_DIR_SENTINEL := $(ROOT_DIR)/build/.no-build-dir-selected
BUILD_DIR_IMPLICIT := $(if $(filter undefined,$(origin BUILD_DIR)),1,0)
BUILD_DIR_CANDIDATES := $(patsubst %/,%,$(sort $(wildcard $(ROOT_DIR)/build/*/)))
MULTIPLE_BUILD_DIR_CANDIDATES := $(word 2,$(BUILD_DIR_CANDIDATES))
ifeq ($(BUILD_DIR_IMPLICIT),1)
BUILD_DIR := $(if $(MULTIPLE_BUILD_DIR_CANDIDATES),$(BUILD_DIR_SENTINEL),$(or $(BUILD_DIR_CANDIDATES),$(BUILD_DIR_SENTINEL)))
endif

# Normalize user-supplied BUILD_DIR values so generated paths and test-runner
# environment variables stay absolute even when callers pass `build/foo/`.
override BUILD_DIR := $(abspath $(BUILD_DIR))
BUILD_DIR_UNSET := $(if $(filter $(BUILD_DIR_SENTINEL),$(BUILD_DIR)),1,)

# Important build-directory-local paths.
CONFIG_MK      := $(BUILD_DIR)/config.mk
WRAPPER_MKFILE := $(BUILD_DIR)/Makefile
CONFIGURE_CMD  := $(BUILD_DIR)/configure.cmd
BIN_DIR        := $(BUILD_DIR)/bin
OBJ_DIR        := $(BUILD_DIR)/obj
GEN_DIR        := $(BUILD_DIR)/generated
TEST_BIN_DIR   := $(OBJ_DIR)/test-bin
TEST_OUTPUT_DIR := $(BUILD_DIR)/test-outputs
COVERAGE_DIR   := $(BUILD_DIR)/coverage
COVERAGE_PROFILE_DIR := $(COVERAGE_DIR)/profiles

# Important targets.
BIN_IMGNEKO     := $(BIN_DIR)/imgneko
BIN_TEST_RUNNER := $(BIN_DIR)/test-runner
BIN_RUN_AND_CHECK := $(BIN_DIR)/run-and-check
TEST_TOOL_NAMES := \
	sample-cli \
	sample-cli-default-no-top-level \
	sample-cli-nocmd \
	sample-cli-no-default \
	sample-cli-top-level-no-default \
	sample-cli-top-level-positional \
	run-in-pty
TEST_TOOL_BINS := $(addprefix $(BIN_DIR)/,$(TEST_TOOL_NAMES))
BUILD_INFO_H    := $(GEN_DIR)/build_info.h

# Additional targets.
COMPILE_DB      := $(BUILD_DIR)/compile_commands.json
STAGED_DEPFILE := $(BUILD_DIR)/dependencies.mk
FINAL_DEPFILE := $(ROOT_DIR)/mk/dependencies.mk
COVERAGE_SUMMARY := $(COVERAGE_DIR)/summary.txt
COVERAGE_UNCOVERED := $(COVERAGE_DIR)/uncovered.qf
COVERAGE_PROFDATA := $(COVERAGE_DIR)/coverage.profdata

# Marks the last successful instrumented test run that populated
# $(COVERAGE_PROFILE_DIR). When covered binaries change, this stamp becomes
# stale so `make coverage` reruns the full suite before rebuilding the report.
COVERAGE_TESTS_STAMP := $(COVERAGE_DIR)/tests.stamp

# Checked-in version file.
VERSION_FILE := $(ROOT_DIR)/VERSION

###############################################################################
# Sources
###############################################################################

APP_SOURCES := src/main.c
IMGNEKO_SOURCES := $(shell if [ -d "$(ROOT_DIR)/src/imgneko" ]; then cd "$(ROOT_DIR)" && find src/imgneko -type f -name '*.c' -print | LC_ALL=C sort; fi)
UTIL_SOURCES := $(shell if [ -d "$(ROOT_DIR)/src/util" ]; then cd "$(ROOT_DIR)" && find src/util -type f -name '*.c' -print | LC_ALL=C sort; fi)
TEST_RUNNER_SOURCE := testing/tools/test-runner.c
RUN_AND_CHECK_SOURCE := testing/tools/run-and-check.c
TEST_TOOL_SOURCES := $(addprefix testing/tools/,$(addsuffix .c,$(TEST_TOOL_NAMES)))
TEST_SUPPORT_SOURCES := $(shell if [ -d "$(ROOT_DIR)/testing/support" ]; then cd "$(ROOT_DIR)" && find testing/support -type f -name '*.c' -print | LC_ALL=C sort; fi)
TEST_SOURCES := $(shell if [ -d "$(ROOT_DIR)/testing/tests" ]; then cd "$(ROOT_DIR)" && find testing/tests -type f -print | LC_ALL=C sort; fi)
TEST_C_SOURCES := $(shell if [ -d "$(ROOT_DIR)/testing/tests" ]; then cd "$(ROOT_DIR)" && find testing/tests -type f -name '*.c' -print | LC_ALL=C sort; fi)

# Preserve the source tree under $(OBJ_DIR), so:
#   src/main.c -> $(OBJ_DIR)/src/main.o
APP_OBJECTS := $(addprefix $(OBJ_DIR)/,$(APP_SOURCES:.c=.o))
IMGNEKO_OBJECTS := $(addprefix $(OBJ_DIR)/,$(IMGNEKO_SOURCES:.c=.o))
UTIL_OBJECTS := $(addprefix $(OBJ_DIR)/,$(UTIL_SOURCES:.c=.o))
SHARED_OBJECTS := $(IMGNEKO_OBJECTS) $(UTIL_OBJECTS)
OBJECTS := $(APP_OBJECTS) $(SHARED_OBJECTS)
TEST_RUNNER_OBJECT := $(OBJ_DIR)/$(TEST_RUNNER_SOURCE:.c=.o)
RUN_AND_CHECK_OBJECT := $(OBJ_DIR)/$(RUN_AND_CHECK_SOURCE:.c=.o)
TEST_TOOL_OBJECTS := $(addprefix $(OBJ_DIR)/,$(TEST_TOOL_SOURCES:.c=.o))
TEST_SUPPORT_OBJECTS := $(addprefix $(OBJ_DIR)/,$(TEST_SUPPORT_SOURCES:.c=.o))
TEST_TOOLS := $(BIN_TEST_RUNNER) $(BIN_RUN_AND_CHECK) $(TEST_TOOL_BINS)
TEST_C_BINS := $(patsubst testing/tests/%.c,$(TEST_BIN_DIR)/%.c.bin,$(TEST_C_SOURCES))
ALL_OBJECTS_AND_BINS := \
		$(OBJECTS) $(TEST_RUNNER_OBJECT) $(RUN_AND_CHECK_OBJECT) \
		$(TEST_TOOL_OBJECTS) \
		$(TEST_SUPPORT_OBJECTS) $(TEST_C_BINS)

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
#   - clean-test-output: should remove stale test logs without requiring configure
#   - clean: should remove outputs even if the build dir was never configured
NO_CONFIG_TARGETS := help clean clean-test-output

# Use the requested goals, or "all" if the user did not name one explicitly.
REQUESTED_GOALS := $(if $(MAKECMDGOALS),$(MAKECMDGOALS),all)

# All other targets are normal build/install targets and require config.mk.
NEEDS_CONFIG := $(filter-out $(NO_CONFIG_TARGETS),$(REQUESTED_GOALS))

# When BUILD_DIR was not set explicitly (for example plain `make` from the
# repository root), require users to disambiguate if ./build contains multiple
# candidate build directories, and otherwise require configure to create one.
ifneq ($(NEEDS_CONFIG),)
ifeq ($(BUILD_DIR_IMPLICIT),1)
ifneq ($(MULTIPLE_BUILD_DIR_CANDIDATES),)
$(error error: found multiple build directories under $(ROOT_DIR)/build; pass BUILD_DIR=<path> explicitly)
endif
ifeq ($(BUILD_DIR_UNSET),1)
$(error error: no build directories found under $(ROOT_DIR)/build; run $(call display_path,$(ROOT_DIR)/configure) first)
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
	-DTEST_RUNNER_BUILD_DIR=\"$(call c_escape,$(BUILD_DIR))\" \
	-DTEST_RUNNER_DEFAULT_JOBS=$(TEST_JOBS)

# Choose the job count from `JOBS`, then `PARALLEL`, then the configured
# `TEST_JOBS` default, and fall back to `1` if none of them are set.
TEST_RUNNER_JOBS := $(or $(strip $(JOBS)),$(strip $(PARALLEL)),$(strip $(TEST_JOBS)),1)

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

# When explicitly enabled by configure, instrument compile and link steps so
# running tests produces Clang source-based coverage data for `make coverage`.
ifeq ($(COVERAGE_REPORT),ON)
COVERAGE_COMPILE_FLAGS = -fprofile-instr-generate -fcoverage-mapping
COVERAGE_LINK_FLAGS = -fprofile-instr-generate
else
COVERAGE_COMPILE_FLAGS =
COVERAGE_LINK_FLAGS =
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
	$(CPPFLAGS) $(FEATURE_CPPFLAGS) $(CFLAGS) $(COVERAGE_COMPILE_FLAGS) \
	$(DEPFILE_COMPILE_FLAGS) $(MJ_COMPILE_FLAGS)
COMMON_LINK_FLAGS = $(COVERAGE_LINK_FLAGS) $(LDFLAGS)

# Emit one #define line per saved configuration variable.
CONFIG_INFO_DEFINES := \
	$(foreach var,PROFILE PREFIX CC CPPFLAGS CFLAGS LDFLAGS LDLIBS FEATURE_X COMP_DB_MJ COVERAGE_REPORT DEPFILES TEST_JOBS, \
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
		echo "error: $(call display_path,$(CONFIG_MK)) does not exist"; \
		echo "       run $(call display_path,$(ROOT_DIR)/configure) --build-dir='$(call display_path,$(BUILD_DIR))' first"; \
		exit 1; \
	fi
	@if [ -f "$(ROOT_DIR)/configure" ]; then \
		if [ "$(CONFIG_MK)" -ot "$(ROOT_DIR)/configure" ]; then \
			echo "error: $(call display_path,$(CONFIG_MK)) is older than $(call display_path,$(ROOT_DIR)/configure). Reconfigure or touch config.mk"; \
			if [ -f "$(CONFIGURE_CMD)" ]; then \
				echo "       rerun: $$(sed 's#$(CURDIR)#.#g' "$(CONFIGURE_CMD)") --force"; \
			fi; \
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
	$(CC) $(COMMON_LINK_FLAGS) -o "$@" $(OBJECTS) $(LDLIBS)

$(BIN_TEST_RUNNER): $(TEST_RUNNER_OBJECT) $(SHARED_OBJECTS) $(CONFIG_MK) $(BUILD_INFO_H) | check-config-date
	@mkdir -p "$(dir $@)"
	$(CC) $(COMMON_LINK_FLAGS) -o "$@" $(TEST_RUNNER_OBJECT) $(SHARED_OBJECTS) $(LDLIBS)

$(BIN_RUN_AND_CHECK): $(RUN_AND_CHECK_OBJECT) $(SHARED_OBJECTS) $(CONFIG_MK) $(BUILD_INFO_H) | check-config-date
	@mkdir -p "$(dir $@)"
	$(CC) $(COMMON_LINK_FLAGS) -o "$@" $(RUN_AND_CHECK_OBJECT) $(SHARED_OBJECTS) $(LDLIBS)

# Link each helper listed in TEST_TOOL_NAMES against the shared utility
# objects. This is a static pattern rule: make expands the explicit target list
# in TEST_TOOL_BINS, then uses the `$(BIN_DIR)/%` pattern to derive the matching
# tool object file path for each binary target.
$(TEST_TOOL_BINS): $(BIN_DIR)/%: $(OBJ_DIR)/testing/tools/%.o $(SHARED_OBJECTS) $(CONFIG_MK) $(BUILD_INFO_H) | check-config-date
	@mkdir -p "$(dir $@)"
	$(CC) $(COMMON_LINK_FLAGS) -o "$@" "$<" $(SHARED_OBJECTS) $(LDLIBS)

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

$(TEST_BIN_DIR)/%.c.bin: testing/tests/%.c $(TEST_SUPPORT_OBJECTS) $(SHARED_OBJECTS) $(CONFIG_MK) $(BUILD_INFO_H) | check-config-date
	@mkdir -p "$(dir $@)"
	$(CC) $(TEST_INCLUDES) $(COMMON_COMPILE_FLAGS) $(COMMON_LINK_FLAGS) "$<" $(TEST_SUPPORT_OBJECTS) $(SHARED_OBJECTS) -o "$@" $(LDLIBS)

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
	@echo "error: depfile generation is disabled in $(call display_path,$(CONFIG_MK)); rerun $(call display_path,$(ROOT_DIR)/configure) --build-dir='$(call display_path,$(BUILD_DIR))' --depfiles"
	@exit 1
endif

###############################################################################
# Main targets
###############################################################################

.DEFAULT_GOAL := all

.PHONY: all install clean coverage coverage-report depfile help check-config-date test test-deps test-list test-tools test-c-bins clean-test-output

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
	if [ -n "$(FILTER)" ]; then set -- "$(FILTER)"; else set -- --all; fi; \
	"$(BIN_TEST_RUNNER)" -j "$(TEST_RUNNER_JOBS)" "$$@"

ifeq ($(COVERAGE_REPORT),ON)
# Run the full instrumented test suite only when any instrumented binary
# changed, then regenerate the merged coverage artifacts only when the raw
# profiles or reporting inputs changed.
$(COVERAGE_TESTS_STAMP): $(BIN_IMGNEKO) $(TEST_TOOLS) $(TEST_C_BINS) $(TEST_SOURCES) | check-config-date
	@rm -rf "$(COVERAGE_PROFILE_DIR)" "$(TEST_OUTPUT_DIR)"
	@mkdir -p "$(COVERAGE_PROFILE_DIR)"
	@LLVM_PROFILE_FILE="$(COVERAGE_PROFILE_DIR)/%m-%p.profraw" "$(BIN_TEST_RUNNER)" -j "$(TEST_RUNNER_JOBS)" --all
	@touch "$@"
	@$(COMPILE_DB_REFRESH)

ifneq ($(strip $(FILTER)),)
coverage: check-config-date
	@echo "error: make coverage does not support FILTER; rerun without FILTER"
	@exit 1
coverage-report: check-config-date
	@echo "error: make coverage-report does not support FILTER; rerun without FILTER"
	@exit 1
else
coverage: check-config-date $(COVERAGE_TESTS_STAMP)
	@$(MAKE) --no-print-directory BUILD_DIR="$(BUILD_DIR)" coverage-report

coverage-report: check-config-date
	@if [ ! -f "$(COVERAGE_TESTS_STAMP)" ]; then \
		echo "error: coverage report inputs are missing in $(call display_path,$(COVERAGE_DIR)); rerun make coverage first"; \
		exit 1; \
	fi
	@"$(ROOT_DIR)/tools/build-coverage-report.sh" \
		"$(ROOT_DIR)" "$(BUILD_DIR)" "$(COVERAGE_DIR)" \
		"$(LLVM_PROFDATA)" "$(LLVM_COV)" \
		"$(BIN_IMGNEKO)" $(TEST_TOOLS) $(TEST_C_BINS)
	@printf '%s\n' "Wrote $(call display_path,$(COVERAGE_SUMMARY))"
	@printf '%s\n' "Wrote $(call display_path,$(COVERAGE_UNCOVERED))"
endif
else
coverage: check-config-date
	@echo "error: coverage report generation is disabled in $(call display_path,$(CONFIG_MK)); rerun $(call display_path,$(ROOT_DIR)/configure) --build-dir='$(call display_path,$(BUILD_DIR))' --coverage-report"
	@exit 1
coverage-report: check-config-date
	@echo "error: coverage report generation is disabled in $(call display_path,$(CONFIG_MK)); rerun $(call display_path,$(ROOT_DIR)/configure) --build-dir='$(call display_path,$(BUILD_DIR))' --coverage-report"
	@exit 1
endif

# List tests.
test-list: check-config-date test-tools test-c-bins
	@set -- --list; \
	if [ -n "$(FILTER)" ]; then set -- "$$@" "$(FILTER)"; fi; \
	"$(BIN_TEST_RUNNER)" "$$@"

# Remove captured per-test output files so each `make test` run starts fresh.
clean-test-output:
	rm -rf "$(TEST_OUTPUT_DIR)"

# Remove build outputs but keep the saved configuration and wrapper Makefile.
clean: clean-test-output
	rm -rf "$(OBJ_DIR)" "$(BIN_DIR)" "$(GEN_DIR)" "$(COVERAGE_DIR)" "$(STAGED_DEPFILE)" "$(COMPILE_DB)"

# Brief user-facing help.
help:
	@printf '%s\n' 'To build and install (build dir will be build/default):'
	@printf '%s\n' '  ./configure --prefix=/usr/local && make && make install'
	@printf '%s\n' ''
	@printf '%s\n' 'To build in a non-default build directory:'
	@printf '%s\n' '  ./configure --build-dir=build/mybuild'
	@printf '%s\n' '  make BUILD_DIR=build/mybuild'
	@printf '%s\n' ''
	@printf '%s\n' 'To build with a non-default profile:'
	@printf '%s\n' '  ./configure --profile=debug'
	@printf '%s\n' '  make BUILD_DIR=build/debug'
	@printf '%s\n' ''
	@printf '%s\n' 'To also generate compile_commands.json during normal builds:'
	@printf '%s\n' '  ./configure --profile=debug --comp-db-mj'
	@printf '%s\n' '  make BUILD_DIR=build/debug'
	@printf '%s\n' ''
	@printf '%s\n' 'To regenerate the checked-in dependency file:'
	@printf '%s\n' '  ./configure --build-dir=build/depfiles --depfiles'
	@printf '%s\n' '  make BUILD_DIR=build/depfiles depfile'
	@printf '%s\n' ''
	@printf '%s\n' 'To run tests from a configured build directory:'
	@printf '%s\n' '  make BUILD_DIR=build/debug test'
	@printf '%s\n' '  make BUILD_DIR=build/debug test JOBS=4'
	@printf '%s\n' '  make BUILD_DIR=build/debug test FILTER='\''test-runner*|some_test.c/subtest'\'''
	@printf '%s\n' '  make BUILD_DIR=build/debug test-list FILTER='\''*.sh|*.test'\'''

// SPDX-License-Identifier: MIT-0

// Print the build metadata embedded by configure.
//
// Include zlib.h before this header when the linked zlib runtime version should
// be included in the output. Callers that do not use zlib incur no dependency
// on its headers.

#ifndef UTIL_PRINT_BUILD_INFO_H
#define UTIL_PRINT_BUILD_INFO_H

#include <stdio.h>

#include "build_info.h"

// Print the common imgneko build information to standard output.
static inline void build_info_print(void) {
    printf("version: %s\n", BUILD_IMGNEKO_VERSION);
    printf("compiled: %s\n", BUILD_COMPILED_AT);
    printf("profile: %s\n", BUILD_CONFIG_PROFILE);
    printf("prefix: %s\n", BUILD_CONFIG_PREFIX);
    printf("cc: %s\n", BUILD_CONFIG_CC);
    printf("cppflags: %s\n", BUILD_CONFIG_CPPFLAGS);
    printf("cflags: %s\n", BUILD_CONFIG_CFLAGS);
    printf("ldflags: %s\n", BUILD_CONFIG_LDFLAGS);
    printf("ldlibs: %s\n", BUILD_CONFIG_LDLIBS);
    printf("zlib_cppflags: %s\n", BUILD_CONFIG_ZLIB_CPPFLAGS);
    printf("zlib_ldlibs: %s\n", BUILD_CONFIG_ZLIB_LDLIBS);
    printf("feature_x: %s\n", BUILD_CONFIG_FEATURE_X);
    printf("coverage_report: %s\n", BUILD_CONFIG_COVERAGE_REPORT);
#ifdef ZLIB_VERSION
    printf("zlib: %s\n", zlibVersion());
#endif
}

#endif

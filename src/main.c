#include <stdio.h>
#include <string.h>

#include "build_info.h"

static int print_version_if_requested(int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("version: %s\n", BUILD_IMGNEKO_VERSION);
            printf("compiled: %s\n", BUILD_COMPILED_AT);
            printf("profile: %s\n", BUILD_CONFIG_PROFILE);
            printf("prefix: %s\n", BUILD_CONFIG_PREFIX);
            printf("cc: %s\n", BUILD_CONFIG_CC);
            printf("cppflags: %s\n", BUILD_CONFIG_CPPFLAGS);
            printf("cflags: %s\n", BUILD_CONFIG_CFLAGS);
            printf("ldflags: %s\n", BUILD_CONFIG_LDFLAGS);
            printf("ldlibs: %s\n", BUILD_CONFIG_LDLIBS);
            printf("feature_x: %s\n", BUILD_CONFIG_FEATURE_X);
            return 1;
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    if (print_version_if_requested(argc, argv)) {
        return 0;
    }

    puts("imgneko!");

    return 0;
}

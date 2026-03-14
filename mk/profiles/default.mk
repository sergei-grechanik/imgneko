# Default configuration profile.
#
# These are the baseline values used when the caller does not request a
# different profile and does not override the variables explicitly.

PREFIX ?= /usr/local
CC ?= cc
CPPFLAGS ?=
CFLAGS ?= -O2 -Wall -Wextra -pedantic
LDFLAGS ?=
FEATURE_X ?= OFF

#define _POSIX_C_SOURCE 200809L

#include <errno.h>

#include "util/time.h"

#include "util/error.h"

double time_monotonic_seconds(void) {
    struct timespec ts;

    require(clock_gettime(CLOCK_MONOTONIC, &ts) == 0,
            "clock_gettime failed: %errno");

    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

struct timespec time_timeout_until_deadline(double deadline_seconds,
                                            double now_seconds) {
    struct timespec timeout = {0};
    double remaining_seconds = deadline_seconds - now_seconds;

    if (remaining_seconds <= 0.0)
        return timeout;

    timeout.tv_sec = (time_t)remaining_seconds;
    timeout.tv_nsec =
        (long)((remaining_seconds - (double)timeout.tv_sec) * 1000000000.0);
    // IMGNEKO_UNCOVERED_OK_START: This is defensive, impossible in practice
    if (timeout.tv_nsec >= 1000000000L) {
        timeout.tv_sec += timeout.tv_nsec / 1000000000L;
        timeout.tv_nsec %= 1000000000L;
    }
    // IMGNEKO_UNCOVERED_OK_END
    if (timeout.tv_sec == 0 && timeout.tv_nsec == 0)
        timeout.tv_nsec = 1;

    return timeout;
}

void time_sleep_seconds(double seconds) {
    struct timespec remaining = {0};

    if (seconds <= 0.0)
        return;

    remaining = time_timeout_until_deadline(seconds, 0.0);
    while (nanosleep(&remaining, &remaining) != 0) {
        require(errno == EINTR, "nanosleep failed: %errno");
    }
}

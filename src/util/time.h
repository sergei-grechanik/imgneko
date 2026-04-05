#ifndef UTIL_TIME_H
#define UTIL_TIME_H

#include <time.h>

// Return the current monotonic time in seconds.
double time_monotonic_seconds(void);

// Return the relative timeout until deadline_seconds. Zero means the deadline
// already expired at now_seconds.
//
// Example:
//   double now = time_monotonic_seconds();
//   struct timespec timeout = time_timeout_until_deadline(now + 1.5, now);
struct timespec time_timeout_until_deadline(double deadline_seconds,
                                            double now_seconds);

// Sleep for a non-negative duration, retrying after signals.
void time_sleep_seconds(double seconds);

#endif

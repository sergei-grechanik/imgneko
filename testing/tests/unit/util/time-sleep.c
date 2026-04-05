#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "util/time.h"

static volatile sig_atomic_t alarm_count = 0;

// Interrupt blocking syscalls without doing any non-async-signal-safe work.
static void handle_alarm(int signum) {
    (void)signum;
    alarm_count++;
}

// Return a failing status after printing one descriptive message.
static int fail_message(const char *message) {
    fprintf(stderr, "%s\n", message);
    return 1;
}

// Verify that repeated SIGALRM delivery interrupts sleep, but the helper keeps
// retrying until the requested duration has fully elapsed.
static int test_sleep_retries_after_eintr(void) {
    struct sigaction action = {0};
    struct sigaction old_action = {0};
    struct itimerval timer = {0};
    struct itimerval old_timer = {0};
    double start_seconds;
    double elapsed_seconds;

    alarm_count = 0;
    action.sa_handler = handle_alarm;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGALRM, &action, &old_action) != 0)
        return fail_message("sigaction(SIGALRM) failed");

    timer.it_value.tv_usec = 10000;
    timer.it_interval.tv_usec = 10000;
    if (setitimer(ITIMER_REAL, &timer, &old_timer) != 0) {
        sigaction(SIGALRM, &old_action, NULL);
        return fail_message("setitimer start failed");
    }

    start_seconds = time_monotonic_seconds();
    time_sleep_seconds(2.0);
    elapsed_seconds = time_monotonic_seconds() - start_seconds;

    timer.it_value.tv_sec = 0;
    timer.it_value.tv_usec = 0;
    timer.it_interval.tv_sec = 0;
    timer.it_interval.tv_usec = 0;
    if (setitimer(ITIMER_REAL, &timer, NULL) != 0) {
        return fail_message("setitimer stop failed");
    }

    sigaction(SIGALRM, &old_action, NULL);
    setitimer(ITIMER_REAL, &old_timer, NULL);

    if (alarm_count < 5)
        return fail_message("sleep was not interrupted often enough");
    if (elapsed_seconds < 1.5)
        return fail_message("sleep returned too early after EINTR");

    return 0;
}

static int list_subtests(void) { return 0; }

// Run one implicit test body used for the `--all` fallback.
static int run_all(void) { return test_sleep_retries_after_eintr(); }

int main(int argc, char **argv) {
    if (argc == 2 && strcmp(argv[1], "--list") == 0)
        return list_subtests();

    if (argc == 2 && strcmp(argv[1], "--all") == 0)
        return run_all();

    fprintf(stderr, "unexpected arguments\n");
    return 1;
}

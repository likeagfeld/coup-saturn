/**
 * cui_test_framework.c - Test framework implementation
 */

#define _POSIX_C_SOURCE 199309L

#include "cui_test_framework.h"
#include <stdlib.h>

#ifdef __APPLE__
#include <mach/mach_time.h>
#elif defined(__linux__)
#include <time.h>
#endif

/* ==========================================================================
   Global State
   ========================================================================== */

cui_test_stats_t cui_test_stats = {0, 0, 0, 0};
cui_test_result_t cui_current_test = {NULL, NULL, 0, true, NULL};

cui_test_entry_t cui_test_registry[CUI_MAX_TESTS];
int cui_test_count = 0;

/* ==========================================================================
   Test Registration
   ========================================================================== */

void cui_register_test(const char* name, cui_test_fn fn)
{
    if (cui_test_count < CUI_MAX_TESTS) {
        cui_test_registry[cui_test_count].name = name;
        cui_test_registry[cui_test_count].fn = fn;
        cui_test_count++;
    } else {
        fprintf(stderr, "Warning: Test registry full, cannot register '%s'\n", name);
    }
}

/* ==========================================================================
   Test Running
   ========================================================================== */

void cui_run_all_tests(void)
{
    printf("========================================\n");
    printf("cui Unit Tests\n");
    printf("========================================\n");
    printf("Running %d registered tests...\n\n", cui_test_count);

    cui_reset_stats();

    for (int i = 0; i < cui_test_count; i++) {
        /* Set up test context */
        cui_current_test.name = cui_test_registry[i].name;
        cui_current_test.file = __FILE__;
        cui_current_test.line = __LINE__;
        cui_current_test.passed = true;
        cui_current_test.message = NULL;

        /* Run the test */
        cui_test_registry[i].fn();

        /* Record result */
        cui_test_stats.total++;
        if (cui_current_test.passed) {
            cui_test_stats.passed++;
            printf("  [PASS] %s\n", cui_current_test.name);
        } else {
            cui_test_stats.failed++;
            printf("  [FAIL] %s (%s:%d)\n", cui_current_test.name,
                   cui_current_test.file, cui_current_test.line);
            if (cui_current_test.message) {
                printf("         %s\n", cui_current_test.message);
            }
        }
    }

    cui_print_summary();
}

void cui_print_summary(void)
{
    printf("\n========================================\n");
    printf("Test Results:\n");
    printf("  Total:   %d\n", cui_test_stats.total);
    printf("  Passed:  %d\n", cui_test_stats.passed);
    printf("  Failed:  %d\n", cui_test_stats.failed);
    printf("  Skipped: %d\n", cui_test_stats.skipped);
    printf("========================================\n");

    if (cui_test_stats.failed > 0) {
        printf("TESTS FAILED\n");
    } else {
        printf("ALL TESTS PASSED\n");
    }
}

void cui_reset_stats(void)
{
    cui_test_stats.total = 0;
    cui_test_stats.passed = 0;
    cui_test_stats.failed = 0;
    cui_test_stats.skipped = 0;
}

/* ==========================================================================
   Performance Measurement
   ========================================================================== */

void cui_perf_start(cui_perf_timer_t* timer)
{
    if (!timer) return;

#ifdef __APPLE__
    timer->start_cycles = mach_absolute_time();
#elif defined(__linux__)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    timer->start_cycles = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#else
    /* Fallback for other platforms */
    timer->start_cycles = 0;
#endif

    timer->end_cycles = 0;
    timer->elapsed_cycles = 0;
}

void cui_perf_stop(cui_perf_timer_t* timer)
{
    if (!timer) return;

#ifdef __APPLE__
    timer->end_cycles = mach_absolute_time();
#elif defined(__linux__)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    timer->end_cycles = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
#else
    timer->end_cycles = 0;
#endif

    timer->elapsed_cycles = timer->end_cycles - timer->start_cycles;
}

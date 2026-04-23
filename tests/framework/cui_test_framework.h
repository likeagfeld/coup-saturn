/**
 * cui Test Framework
 *
 * Minimal test framework that supports auto-registration and works
 * with the cui library's mock PAL for testing UI components.
 */

#ifndef CUI_TEST_FRAMEWORK_H
#define CUI_TEST_FRAMEWORK_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ==========================================================================
   Test Result Tracking
   ========================================================================== */

typedef struct {
    const char* name;
    const char* file;
    int line;
    bool passed;
    const char* message;
} cui_test_result_t;

typedef struct {
    int total;
    int passed;
    int failed;
    int skipped;
} cui_test_stats_t;

/* Global test state */
extern cui_test_stats_t cui_test_stats;
extern cui_test_result_t cui_current_test;

/* ==========================================================================
   Test Macros
   ========================================================================== */

/**
 * Define a test function with auto-registration.
 * Usage: CUI_TEST(my_test_name) { ... test code ... }
 */
#define CUI_TEST(name) \
    void test_##name(void); \
    static void __attribute__((constructor)) register_test_##name(void) { \
        cui_register_test(#name, test_##name); \
    } \
    void test_##name(void)

/**
 * Begin a test within a test function.
 * Sets up the current test context for assertions.
 */
#define CUI_TEST_BEGIN(name) \
    do { \
        cui_current_test.name = name; \
        cui_current_test.file = __FILE__; \
        cui_current_test.line = __LINE__; \
        cui_current_test.passed = true; \
        cui_current_test.message = NULL; \
    } while(0)

/**
 * End a test and record the result.
 */
#define CUI_TEST_END() \
    do { \
        cui_test_stats.total++; \
        if (cui_current_test.passed) { \
            cui_test_stats.passed++; \
            printf("  [PASS] %s\n", cui_current_test.name); \
        } else { \
            cui_test_stats.failed++; \
            printf("  [FAIL] %s (%s:%d)\n", cui_current_test.name, \
                   cui_current_test.file, cui_current_test.line); \
            if (cui_current_test.message) { \
                printf("         %s\n", cui_current_test.message); \
            } \
        } \
    } while(0)

/* ==========================================================================
   Assertion Macros
   ========================================================================== */

/**
 * Assert a condition is true.
 */
#define CUI_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            cui_current_test.passed = false; \
            cui_current_test.line = __LINE__; \
            cui_current_test.message = "Assertion failed: " #expr; \
            return; \
        } \
    } while(0)

/**
 * Assert two values are equal.
 */
#define CUI_ASSERT_EQ(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            cui_current_test.passed = false; \
            cui_current_test.line = __LINE__; \
            cui_current_test.message = "Expected " #expected " == " #actual; \
            return; \
        } \
    } while(0)

/**
 * Assert two values are not equal.
 */
#define CUI_ASSERT_NEQ(a, b) \
    do { \
        if ((a) == (b)) { \
            cui_current_test.passed = false; \
            cui_current_test.line = __LINE__; \
            cui_current_test.message = "Expected " #a " != " #b; \
            return; \
        } \
    } while(0)

/**
 * Assert a < b.
 */
#define CUI_ASSERT_LT(a, b) \
    do { \
        if (!((a) < (b))) { \
            cui_current_test.passed = false; \
            cui_current_test.line = __LINE__; \
            cui_current_test.message = "Expected " #a " < " #b; \
            return; \
        } \
    } while(0)

/**
 * Assert a > b.
 */
#define CUI_ASSERT_GT(a, b) \
    do { \
        if (!((a) > (b))) { \
            cui_current_test.passed = false; \
            cui_current_test.line = __LINE__; \
            cui_current_test.message = "Expected " #a " > " #b; \
            return; \
        } \
    } while(0)

/**
 * Assert a <= b.
 */
#define CUI_ASSERT_LE(a, b) \
    do { \
        if (!((a) <= (b))) { \
            cui_current_test.passed = false; \
            cui_current_test.line = __LINE__; \
            cui_current_test.message = "Expected " #a " <= " #b; \
            return; \
        } \
    } while(0)

/**
 * Assert a >= b.
 */
#define CUI_ASSERT_GE(a, b) \
    do { \
        if (!((a) >= (b))) { \
            cui_current_test.passed = false; \
            cui_current_test.line = __LINE__; \
            cui_current_test.message = "Expected " #a " >= " #b; \
            return; \
        } \
    } while(0)

/**
 * Assert pointer is NULL.
 */
#define CUI_ASSERT_NULL(ptr) \
    do { \
        if ((ptr) != NULL) { \
            cui_current_test.passed = false; \
            cui_current_test.line = __LINE__; \
            cui_current_test.message = "Expected " #ptr " to be NULL"; \
            return; \
        } \
    } while(0)

/**
 * Assert pointer is not NULL.
 */
#define CUI_ASSERT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == NULL) { \
            cui_current_test.passed = false; \
            cui_current_test.line = __LINE__; \
            cui_current_test.message = "Expected " #ptr " to not be NULL"; \
            return; \
        } \
    } while(0)

/**
 * Assert two strings are equal.
 */
#define CUI_ASSERT_STR_EQ(actual, expected) \
    do { \
        if (strcmp((actual), (expected)) != 0) { \
            cui_current_test.passed = false; \
            cui_current_test.line = __LINE__; \
            cui_current_test.message = "Expected strings to be equal: " #actual " == " #expected; \
            return; \
        } \
    } while(0)

/**
 * Assert two strings are not equal.
 */
#define CUI_ASSERT_STR_NEQ(a, b) \
    do { \
        if (strcmp((a), (b)) == 0) { \
            cui_current_test.passed = false; \
            cui_current_test.line = __LINE__; \
            cui_current_test.message = "Expected strings to differ: " #a " != " #b; \
            return; \
        } \
    } while(0)

/**
 * Assert two characters are equal.
 */
#define CUI_ASSERT_CHAR_EQ(actual, expected) \
    do { \
        if ((actual) != (expected)) { \
            cui_current_test.passed = false; \
            cui_current_test.line = __LINE__; \
            cui_current_test.message = "Expected chars to be equal: " #actual " == " #expected; \
            return; \
        } \
    } while(0)

/**
 * Assert a boolean is true.
 */
#define CUI_ASSERT_TRUE(expr) CUI_ASSERT(expr)

/**
 * Assert a boolean is false.
 */
#define CUI_ASSERT_FALSE(expr) \
    do { \
        if (expr) { \
            cui_current_test.passed = false; \
            cui_current_test.line = __LINE__; \
            cui_current_test.message = "Expected " #expr " to be false"; \
            return; \
        } \
    } while(0)

/* ==========================================================================
   Test Registration and Running
   ========================================================================== */

typedef void (*cui_test_fn)(void);

typedef struct {
    const char* name;
    cui_test_fn fn;
} cui_test_entry_t;

#define CUI_MAX_TESTS 2048

extern cui_test_entry_t cui_test_registry[CUI_MAX_TESTS];
extern int cui_test_count;

/**
 * Register a test function (called automatically by CUI_TEST macro).
 */
void cui_register_test(const char* name, cui_test_fn fn);

/**
 * Run all registered tests.
 */
void cui_run_all_tests(void);

/**
 * Print test summary.
 */
void cui_print_summary(void);

/**
 * Reset test statistics (useful for running specific test suites).
 */
void cui_reset_stats(void);

/* ==========================================================================
   Performance Measurement
   ========================================================================== */

typedef struct {
    uint64_t start_cycles;
    uint64_t end_cycles;
    uint64_t elapsed_cycles;
} cui_perf_timer_t;

/**
 * Start the performance timer.
 */
void cui_perf_start(cui_perf_timer_t* timer);

/**
 * Stop the performance timer and calculate elapsed cycles.
 */
void cui_perf_stop(cui_perf_timer_t* timer);

/**
 * Assert elapsed cycles are less than a threshold.
 */
#define CUI_ASSERT_CYCLES_LT(timer, max_cycles) \
    do { \
        if ((timer).elapsed_cycles >= (max_cycles)) { \
            cui_current_test.passed = false; \
            cui_current_test.line = __LINE__; \
            cui_current_test.message = "Exceeded cycle budget"; \
            return; \
        } \
    } while(0)

/* ==========================================================================
   Test Suite Support (for legacy test files during migration)
   ========================================================================== */

/**
 * Run a named test with automatic setup/teardown.
 * This is a helper for test files that use the traditional run_xxx_tests() pattern.
 */
#define CUI_RUN_TEST(name, setup_fn, teardown_fn) \
    do { \
        CUI_TEST_BEGIN(#name); \
        setup_fn(); \
        test_##name(); \
        teardown_fn(); \
        CUI_TEST_END(); \
    } while(0)

#endif /* CUI_TEST_FRAMEWORK_H */

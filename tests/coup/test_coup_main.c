/**
 * test_coup_main.c - Test runner for Coup rule engine tests
 *
 * Uses the centralized test framework with auto-registration.
 * All tests defined with CUI_TEST() in linked files are automatically registered.
 */

#include "cui_test_framework.h"
#include <stdlib.h>

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    cui_run_all_tests();

    return (cui_test_stats.failed > 0) ? EXIT_FAILURE : EXIT_SUCCESS;
}

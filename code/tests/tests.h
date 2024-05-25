#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>

extern int tests_run;

#define mu_assert_str_starts_with(message, actual, expected) \
    do { \
        if (!(actual) || !(expected)) { \
            fprintf(stderr, "%s\nInvalid NULL pointer passed for strings.\nActual: '%s'\nExpected to start with: '%s'\n", \
                    message, (actual) ? (actual) : "NULL", (expected) ? (expected) : "NULL"); \
            exit(EXIT_FAILURE); \
        } else { \
            size_t expected_len = strlen(expected); \
            if (strncmp((actual), (expected), expected_len) != 0) { \
                fprintf(stderr, "%s\nActual: '%s'\nExpected to start with: '%s'\n", message, actual, expected); \
                exit(EXIT_FAILURE); \
            } \
        } \
    } while (0)

#define mu_assert_str_eq(message, actual, expected) \
    do { \
        if (!(actual) || !(expected)) { \
            fprintf(stderr, "%s\nInvalid NULL pointer passed for strings.\nActual: '%s'\nExpected: '%s'\n", \
                    message, (actual) ? (actual) : "NULL", (expected) ? (expected) : "NULL"); \
            exit(EXIT_FAILURE); \
        } else if (strcmp((actual), (expected)) != 0) { \
            fprintf(stderr, "%s\nActual: '%s'\nExpected: '%s'\n", message, actual, expected); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

#define mu_assert_int_eq(message, actual, expected) \
    do { \
        if ((actual) != (expected)) { \
            fprintf(stderr, "%s\nActual: %ld\nExpected: %ld\n", message, (long) actual, (long) expected); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

#define mu_assert_int_gt(message, actual, expected) \
    do { \
        if ((actual) <= (expected)) { \
            fprintf(stderr, "%s\nActual: %ld\nExpected greater than: %ld\n", message, (long) actual, (long) expected); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

#define mu_assert(message, test) \
    do { \
        if (!(test)) { \
            fprintf(stderr, "%s\nAssertion failed: %s\n", message, #test); \
            exit(EXIT_FAILURE); \
        } \
    } while (0)

#define mu_run_test(test) \
    do { \
        tests_run++; \
        fprintf(stderr, "========================================\n"); \
        fprintf(stderr, "Running test: %s\n", #test); \
        clock_t start_time = clock(); \
        test(); \
        clock_t end_time = clock(); \
        double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC; \
        fprintf(stderr, "Test %s passed.\n", #test); \
        fprintf(stderr, "Elapsed time: %.4f seconds\n", elapsed_time); \
        fprintf(stderr, "========================================\n\n"); \
    } while (0)

void testPrint(void);

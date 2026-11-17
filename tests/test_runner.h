#ifndef AHSD_TEST_RUNNER_H
#define AHSD_TEST_RUNNER_H

#include <stdio.h>
#include <stdlib.h>

static int g_tests_run = 0;
static int g_tests_failed = 0;

#define ASSERT_TRUE(cond)                                                                      \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            fprintf(stderr, "  FAIL %s:%d: expected (%s) to be true\n", __FILE__, __LINE__,    \
                    #cond);                                                                    \
            g_tests_failed++;                                                                  \
            return;                                                                            \
        }                                                                                      \
    } while (0)

#define ASSERT_EQ_INT(expected, actual)                                                        \
    do {                                                                                       \
        long long _exp = (long long)(expected);                                                \
        long long _act = (long long)(actual);                                                  \
        if (_exp != _act) {                                                                    \
            fprintf(stderr, "  FAIL %s:%d: expected %lld, got %lld\n", __FILE__, __LINE__,     \
                    _exp, _act);                                                               \
            g_tests_failed++;                                                                  \
            return;                                                                            \
        }                                                                                      \
    } while (0)

#define ASSERT_EQ_SIZE(expected, actual)                                                       \
    do {                                                                                       \
        size_t _exp = (size_t)(expected);                                                      \
        size_t _act = (size_t)(actual);                                                        \
        if (_exp != _act) {                                                                    \
            fprintf(stderr, "  FAIL %s:%d: expected %zu, got %zu\n", __FILE__, __LINE__, _exp, \
                    _act);                                                                     \
            g_tests_failed++;                                                                  \
            return;                                                                            \
        }                                                                                      \
    } while (0)

#define RUN_TEST(fn)                                                                           \
    do {                                                                                       \
        int _before = g_tests_failed;                                                          \
        g_tests_run++;                                                                         \
        fn();                                                                                  \
        if (g_tests_failed == _before) {                                                       \
            printf("  PASS %s\n", #fn);                                                        \
        }                                                                                      \
    } while (0)

#endif

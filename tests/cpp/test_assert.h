#ifndef FUSION_TESTS_TEST_ASSERT_H
#define FUSION_TESTS_TEST_ASSERT_H

#include <iostream>

// Minimal assertion harness -- no external test-framework dependency. A
// failing CHECK prints its expression and location and increments the
// caller's own failure counter, but does not stop the test function, so one
// failing check doesn't hide the next one in the same run.
#define CHECK(condition, failureCounter) \
    do { \
        if (!(condition)) { \
            std::cerr << "  FAIL: " << #condition << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            ++(failureCounter); \
        } \
    } while (0)

#endif // FUSION_TESTS_TEST_ASSERT_H

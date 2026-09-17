#include "fusion/metrics/PointCloudStats.h"
#include "test_assert.h"

#include <cmath>
#include <iostream>
#include <string>

int RunStrataStatBundleTests() {
    int failures = 0;
    std::cout << "StrataStatBundle tests\n";

    // An empty bucket reports count/proportion of 0 without dividing by
    // zero, and leaves mean/stddev/min/max at their struct defaults for the
    // caller to resolve against its own /noheight sentinel.
    {
        fusion::metrics::StrataStatBundle b = fusion::metrics::ComputeStrataStatBundle({}, 100);
        CHECK(b.count == 0, failures);
        CHECK(b.proportion == 0.0, failures);
    }

    // A non-empty bucket: mean/stddev/min/max match a hand-computed result,
    // and proportion divides by the whole-cloud total passed in, not by the
    // bucket's own size.
    {
        std::vector<double> vals = {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0};
        fusion::metrics::StrataStatBundle b = fusion::metrics::ComputeStrataStatBundle(vals, 40);
        CHECK(b.count == 8, failures);
        CHECK(std::abs(b.proportion - 0.2) < 1e-9, failures);
        CHECK(std::abs(b.mean - 5.0f) < 1e-4, failures);
        CHECK(std::abs(b.stddev - 2.0f) < 1e-4, failures);
        CHECK(b.min == 2.0f, failures);
        CHECK(b.max == 9.0f, failures);
    }

    // Column names and value order stay in lockstep with each other.
    {
        auto names = fusion::metrics::StrataStatBundleColumnNames("stratum_00_");
        fusion::metrics::StrataStatBundle b = fusion::metrics::ComputeStrataStatBundle({1.0, 2.0, 3.0}, 3);
        auto vals = fusion::metrics::StrataStatBundleAsVector(b);
        CHECK(names.size() == 6, failures);
        CHECK(vals.size() == 6, failures);
        CHECK(names[0] == "stratum_00_count", failures);
        CHECK(names[5] == "stratum_00_max", failures);
    }

    std::cout << (failures == 0 ? "  all passed\n" : ("  " + std::to_string(failures) + " failure(s)\n"));
    return failures;
}

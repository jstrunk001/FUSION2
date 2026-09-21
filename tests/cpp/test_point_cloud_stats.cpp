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

// GetMode() itself is a file-local helper inside PointCloudStats.cpp, not
// exposed through the header, so these exercise it indirectly through
// ComputePointStatBundle()'s .mode field -- the only place it's called from.
// GetMode was rewritten from a std::map<int,int>-based grouping pass to a
// single allocation-free scan over already-sorted data (see the comment
// above GetMode() in PointCloudStats.cpp); these checks pin down that the
// rewrite still finds the correct most-populous bin, and still breaks a
// count tie the same way the map-based version did -- in favor of the
// lower (map key order is ascending, so it's the earliest-visited) bin.
int RunGetModeTests() {
    int failures = 0;
    std::cout << "GetMode (via ComputePointStatBundle) tests\n";

    // {1,1,1,2,3}: with the default 0.5 bin width, all three 1.0 values
    // land in one bin (3 points) while 2.0 and 3.0 each land alone in their
    // own bin (1 point each) -- the 1.0 bin is the clear, single winner.
    {
        fusion::metrics::PointStatBundle b = fusion::metrics::ComputePointStatBundle({1.0f, 1.0f, 1.0f, 2.0f, 3.0f});
        CHECK(std::abs(b.mode - 1.25f) < 1e-4, failures);
    }

    // {0,0,1,1}: two bins (one for the pair of 0.0's, one for the pair of
    // 1.0's) tie at 2 points each -- the lower bin (0.0's) must win.
    {
        fusion::metrics::PointStatBundle b = fusion::metrics::ComputePointStatBundle({0.0f, 0.0f, 1.0f, 1.0f});
        CHECK(std::abs(b.mode - 0.25f) < 1e-4, failures);
    }

    // A single value is trivially its own (only) bin's mode.
    {
        fusion::metrics::PointStatBundle b = fusion::metrics::ComputePointStatBundle({5.0f});
        CHECK(std::abs(b.mode - 5.25f) < 1e-4, failures);
    }

    std::cout << (failures == 0 ? "  all passed\n" : ("  " + std::to_string(failures) + " failure(s)\n"));
    return failures;
}

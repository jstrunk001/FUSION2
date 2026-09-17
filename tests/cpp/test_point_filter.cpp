#include "fusion/lidar/PointFilter.h"
#include "test_assert.h"

#include <iostream>
#include <string>

namespace {

fusion::lidar::PointRecord MakePoint(uint8_t classification, uint8_t returnNumber = 1,
                                      uint8_t numberOfReturns = 1, bool withheld = false) {
    fusion::lidar::PointRecord pt;
    pt.classification = classification;
    pt.returnNumber = returnNumber;
    pt.numberOfReturns = numberOfReturns;
    pt.withheld = withheld;
    return pt;
}

} // namespace

int RunPointFilterTests() {
    int failures = 0;
    std::cout << "PointFilter tests\n";

    // Default (no /class, no /return): noise classes 7 and 18 excluded,
    // withheld points excluded, every other class and return kept.
    {
        auto filter = fusion::lidar::PointFilter::Parse(std::nullopt, std::nullopt);
        CHECK(filter.Keep(MakePoint(2)), failures);
        CHECK(filter.Keep(MakePoint(1)), failures);
        CHECK(!filter.Keep(MakePoint(7)), failures);
        CHECK(!filter.Keep(MakePoint(18)), failures);
        CHECK(!filter.Keep(MakePoint(2, 1, 1, /*withheld=*/true)), failures);
    }

    // /class:all and /class:* both disable the default noise exclusion.
    {
        auto allFilter = fusion::lidar::PointFilter::Parse(std::string("all"), std::nullopt);
        CHECK(allFilter.Keep(MakePoint(7)), failures);
        CHECK(allFilter.Keep(MakePoint(18)), failures);

        auto starFilter = fusion::lidar::PointFilter::Parse(std::string("*"), std::nullopt);
        CHECK(starFilter.Keep(MakePoint(7)), failures);
    }

    // /class:~7,9,18 -- blacklist: keep everything except the listed classes.
    {
        auto filter = fusion::lidar::PointFilter::Parse(std::string("~7,9,18"), std::nullopt);
        CHECK(!filter.Keep(MakePoint(7)), failures);
        CHECK(!filter.Keep(MakePoint(9)), failures);
        CHECK(!filter.Keep(MakePoint(18)), failures);
        CHECK(filter.Keep(MakePoint(2)), failures);
    }

    // /class:1-5 -- whitelist range; classes outside it are dropped even
    // when they aren't one of the default noise classes.
    {
        auto filter = fusion::lidar::PointFilter::Parse(std::string("1-5"), std::nullopt);
        CHECK(filter.Keep(MakePoint(1)), failures);
        CHECK(filter.Keep(MakePoint(5)), failures);
        CHECK(!filter.Keep(MakePoint(6)), failures);
    }

    // /class:1,2,3 -- whitelist list form (no range).
    {
        auto filter = fusion::lidar::PointFilter::Parse(std::string("1,2,3"), std::nullopt);
        CHECK(filter.Keep(MakePoint(2)), failures);
        CHECK(!filter.Keep(MakePoint(4)), failures);
    }

    // /return mnemonics: first, last, only, intermediate.
    {
        auto first = fusion::lidar::PointFilter::Parse(std::nullopt, std::string("first"));
        CHECK(first.Keep(MakePoint(2, 1, 3)), failures);
        CHECK(!first.Keep(MakePoint(2, 2, 3)), failures);

        auto last = fusion::lidar::PointFilter::Parse(std::nullopt, std::string("last"));
        CHECK(last.Keep(MakePoint(2, 3, 3)), failures);
        CHECK(!last.Keep(MakePoint(2, 2, 3)), failures);

        auto only = fusion::lidar::PointFilter::Parse(std::nullopt, std::string("only"));
        CHECK(only.Keep(MakePoint(2, 1, 1)), failures);
        CHECK(!only.Keep(MakePoint(2, 1, 2)), failures);

        auto intermediate = fusion::lidar::PointFilter::Parse(std::nullopt, std::string("intermediate"));
        CHECK(intermediate.Keep(MakePoint(2, 2, 3)), failures);
        CHECK(!intermediate.Keep(MakePoint(2, 1, 3)), failures);
        CHECK(!intermediate.Keep(MakePoint(2, 3, 3)), failures);
    }

    // /return:1,2 -- explicit numeric whitelist.
    {
        auto filter = fusion::lidar::PointFilter::Parse(std::nullopt, std::string("1,2"));
        CHECK(filter.Keep(MakePoint(2, 1, 4)), failures);
        CHECK(filter.Keep(MakePoint(2, 2, 4)), failures);
        CHECK(!filter.Keep(MakePoint(2, 3, 4)), failures);
    }

    std::cout << (failures == 0 ? "  all passed\n" : ("  " + std::to_string(failures) + " failure(s)\n"));
    return failures;
}

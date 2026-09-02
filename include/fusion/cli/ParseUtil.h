#ifndef FUSION_CLI_PARSEUTIL_H
#define FUSION_CLI_PARSEUTIL_H

#include <string>
#include <sstream>
#include <vector>
#include <unordered_set>

namespace fusion::cli {

// Parses a comma-separated list of numbers, e.g. "0.5,2.0,5.0" -> {0.5, 2.0, 5.0}.
// Used for options like /strata and /outlier. Unparseable items are skipped.
inline std::vector<double> ParseFloatList(const std::string& str) {
    std::vector<double> values;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            try {
                values.push_back(std::stod(item));
            } catch (...) {}
        }
    }
    return values;
}

// Parses a comma-separated list of integers into a lookup set, e.g. "2,3,4,5" -> {2,3,4,5}.
// Used for options like /class. Unparseable items are skipped.
inline std::unordered_set<int> ParseIntSet(const std::string& str) {
    std::unordered_set<int> values;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            try {
                values.insert(std::stoi(item));
            } catch (...) {}
        }
    }
    return values;
}

} // namespace fusion::cli

#endif // FUSION_CLI_PARSEUTIL_H

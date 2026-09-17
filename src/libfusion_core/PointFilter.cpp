#include "fusion/lidar/PointFilter.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace fusion::lidar {

namespace {

constexpr int kDefaultNoiseClasses[] = {7, 18};

std::string ToLowerNoSpace(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (unsigned char c : s) {
        if (!std::isspace(c)) result.push_back(static_cast<char>(std::tolower(c)));
    }
    return result;
}

// Sets target[lo..hi] to value for a single token, which is either a plain
// integer ("18") or an inclusive range ("1-5"). Shared by /class and
// /return's explicit-list parsing so range syntax works identically for
// both options. Out-of-range endpoints are clamped rather than rejected, so
// a typo like /class:1-999 still does something sensible.
void ApplyRangeOrValue(const std::string& token, std::array<bool, 256>& target, bool value) {
    if (token.empty()) return;
    size_t dash = token.find('-', 1); // start at 1: a leading '-' isn't a range separator here
    int lo = 0, hi = 0;
    try {
        if (dash != std::string::npos) {
            lo = std::stoi(token.substr(0, dash));
            hi = std::stoi(token.substr(dash + 1));
        } else {
            lo = hi = std::stoi(token);
        }
    } catch (...) {
        return;
    }
    if (lo > hi) std::swap(lo, hi);
    lo = std::max(lo, 0);
    hi = std::min(hi, 255);
    for (int v = lo; v <= hi; ++v) target[static_cast<size_t>(v)] = value;
}

void ParseClassSpec(const std::optional<std::string>& spec, std::array<bool, 256>& allowedClasses) {
    allowedClasses.fill(true);
    for (int noiseClass : kDefaultNoiseClasses) allowedClasses[noiseClass] = false;
    if (!spec.has_value()) return; // default: every class except noise 7/18

    std::string raw = ToLowerNoSpace(*spec);
    if (raw.empty() || raw == "all" || raw == "*") {
        allowedClasses.fill(true);
        return;
    }

    bool blacklist = raw.front() == '~';
    if (blacklist) raw.erase(0, 1);
    allowedClasses.fill(blacklist);

    std::stringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, ',')) {
        ApplyRangeOrValue(token, allowedClasses, !blacklist);
    }
}

ReturnFilterMode ParseReturnSpec(const std::optional<std::string>& spec, std::array<bool, 256>& allowedReturns) {
    allowedReturns.fill(true);
    if (!spec.has_value()) return ReturnFilterMode::Any;

    std::string raw = ToLowerNoSpace(*spec);
    if (raw.empty()) return ReturnFilterMode::Any;
    if (raw == "first") return ReturnFilterMode::First;
    if (raw == "last") return ReturnFilterMode::Last;
    if (raw == "only") return ReturnFilterMode::Only;
    if (raw == "intermediate") return ReturnFilterMode::Intermediate;

    allowedReturns.fill(false);
    std::stringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, ',')) {
        ApplyRangeOrValue(token, allowedReturns, true);
    }
    return ReturnFilterMode::ExplicitSet;
}

} // namespace

PointFilter::PointFilter() {
    m_allowedClasses.fill(true);
    for (int noiseClass : kDefaultNoiseClasses) m_allowedClasses[noiseClass] = false;
    m_allowedReturns.fill(true);
}

PointFilter PointFilter::Parse(const std::optional<std::string>& classSpec, const std::optional<std::string>& returnSpec) {
    PointFilter filter;
    ParseClassSpec(classSpec, filter.m_allowedClasses);
    filter.m_returnMode = ParseReturnSpec(returnSpec, filter.m_allowedReturns);
    return filter;
}

PointFilter PointFilter::FromParser(const fusion::cli::ArgumentParser& parser) {
    return Parse(parser.GetOption("class"), parser.GetOption("return"));
}

void PointFilter::RegisterOptions(fusion::cli::ArgumentParser& parser) {
    parser.AddOption("class",
        "Point classifications to keep. Omit for the default (excludes ASPRS "
        "noise classes 7 and 18); /class:all or /class:* keeps every class; "
        "/class:2,3,4,5 or /class:1-5 whitelists the listed classes; "
        "/class:~7,9,18 blacklists them (keeps every other class).");
    parser.AddOption("return",
        "Return numbers to keep: /return:1 or /return:1,2 for an explicit "
        "list (ranges like /return:1-2 also work), or the mnemonics "
        "/return:first, /return:last, /return:only, /return:intermediate. "
        "Omit to keep every return.");
}

} // namespace fusion::lidar

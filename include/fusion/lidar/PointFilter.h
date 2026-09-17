#ifndef FUSION_LIDAR_POINTFILTER_H
#define FUSION_LIDAR_POINTFILTER_H

#include <array>
#include <optional>
#include <string>

#include "fusion/cli/ArgumentParser.h"
#include "fusion/lidar/LASPointCloud.h"

namespace fusion::lidar {

// How /return selects points: an explicit whitelist of return-number values
// (a single-field lookup), or a legacy FUSION mnemonic that depends on both
// returnNumber and numberOfReturns together, so it can't be reduced to a
// lookup table the way classification and explicit /return values can.
enum class ReturnFilterMode {
    Any,          // no /return given -- every return number passes
    ExplicitSet,  // /return:1 or /return:1,2 -- allowedReturns lookup
    First,        // /return:first  -- returnNumber == 1
    Last,         // /return:last   -- returnNumber == numberOfReturns
    Only,         // /return:only   -- numberOfReturns == 1
    Intermediate  // /return:intermediate -- 1 < returnNumber < numberOfReturns
};

// Centralizes classification and return-number filtering so every tool
// applies identical /class and /return semantics instead of each
// reimplementing its own whitelist. Legacy FUSION syntax:
//   /class            (omitted)  -- default: excludes ASPRS noise classes 7
//                                    (low point) and 18 (high noise)
//   /class:all, /class:*         -- disables the default noise exclusion,
//                                    keeps every classification code
//   /class:2,3,4,5 or /class:1-5 -- whitelist: keep only the listed classes
//   /class:~7,9,18                -- blacklist: keep every class except the
//                                    listed ones (replaces the 7/18 default)
//   /return           (omitted)  -- keep every return
//   /return:1 or /return:1,2     -- explicit return-number whitelist
//   /return:first/last/only/intermediate -- legacy FUSION mnemonics
// Withheld points (the LAS "discard this point" flag) are excluded by
// default regardless of /class, since that bit means the same thing as an
// ASPRS noise classification but is stored as its own field on PointRecord.
// allowedClasses/allowedReturns are precomputed once per run, so Keep()
// costs a couple of array lookups per point, not a parse or set search.
class PointFilter {
public:
    PointFilter();

    // classSpec/returnSpec are the raw option strings as returned by
    // ArgumentParser::GetOption -- pass std::nullopt for an omitted option
    // so the default noise-exclusion / keep-all-returns behavior applies.
    static PointFilter Parse(const std::optional<std::string>& classSpec,
                              const std::optional<std::string>& returnSpec);

    // Convenience most tools use directly: reads "class" and "return"
    // straight off the tool's own parser.
    static PointFilter FromParser(const fusion::cli::ArgumentParser& parser);

    // Registers the /class and /return options (with shared help text) on a
    // tool's ArgumentParser -- call once, before parser.Parse().
    static void RegisterOptions(fusion::cli::ArgumentParser& parser);

    inline bool Keep(const PointRecord& pt) const {
        if (m_excludeWithheld && pt.withheld) return false;
        if (!m_allowedClasses[pt.classification]) return false;
        return KeepReturn(pt);
    }

private:
    inline bool KeepReturn(const PointRecord& pt) const {
        switch (m_returnMode) {
            case ReturnFilterMode::Any: return true;
            case ReturnFilterMode::ExplicitSet: return m_allowedReturns[pt.returnNumber];
            case ReturnFilterMode::First: return pt.returnNumber == 1;
            case ReturnFilterMode::Last: return pt.returnNumber == pt.numberOfReturns;
            case ReturnFilterMode::Only: return pt.numberOfReturns == 1;
            case ReturnFilterMode::Intermediate: return pt.returnNumber > 1 && pt.returnNumber < pt.numberOfReturns;
        }
        return true;
    }

    std::array<bool, 256> m_allowedClasses;
    std::array<bool, 256> m_allowedReturns;
    ReturnFilterMode m_returnMode{ReturnFilterMode::Any};
    bool m_excludeWithheld{true};
};

} // namespace fusion::lidar

#endif // FUSION_LIDAR_POINTFILTER_H

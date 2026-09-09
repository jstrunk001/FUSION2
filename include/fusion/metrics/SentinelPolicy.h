#ifndef FUSION_METRICS_SENTINELPOLICY_H
#define FUSION_METRICS_SENTINELPOLICY_H

#include <algorithm>
#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>

namespace fusion::metrics {

// A concrete sentinel value resolved from a /nodata or /noheight option
// string. "NA" (case-insensitive) resolves to quiet_NaN() with isNA = true,
// so CSV output writes the literal text "NA" rather than a number. Anything
// else -- including "inf"/"-inf", which std::stof already parses directly
// per the C++11 standard, needing no special-case code -- parses as a
// literal float (e.g. "0", "-9999", "inf") with isNA = false, written as
// that number in both CSV and raster output.
struct SentinelValue {
    float value;
    bool isNA;
};

inline SentinelValue ParseSentinelOption(const std::string& raw) {
    std::string upper = raw;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (upper == "NA") {
        return {std::numeric_limits<float>::quiet_NaN(), true};
    }
    return {std::stof(raw), false};
}

// Both gridmetrics and cloudmetrics add the same two options:
//   parser.AddOption("nodata", "Value for cells/clouds with insufficient
//     points to compute metrics: NA, or a number such as 0, -9999, or inf", "NA");
//   parser.AddOption("noheight", "Value for height-dependent metrics when
//     points exist but none clear the height cutoff: NA, or a number such
//     as 0, -9999, or inf", "0");
// then build a SentinelPolicy from the parsed strings via ParseSentinelOption.
//
// The rule, applied uniformly:
// - A cell/cloud with zero total points (nothing landed there at all) gets
//   `nodata` written to every band/column, including counts and density.
// - A cell/cloud with points present but none pass the height filter gets
//   `noheight` written specifically to the bands whose only data source is
//   the height-filtered point set (elev_*, int_*, per-stratum stats, etc).
// - Metrics well-defined directly from the unfiltered return count
//   (canopy_cover, point_density, per-return-number counts, cover-variant
//   bands) are never overridden by `noheight` -- a computed 0% cover when
//   points exist but none clear the cutoff is a real answer, not a
//   placeholder.
//
// GDAL raster caveat: GDALRaster::Create registers exactly one NoData value
// per file. `nodata`'s resolved value becomes that file's registered
// NoData; `noheight`'s value (when different and non-NA) is written as an
// ordinary, valid pixel, not flagged as NoData by the GeoTIFF header. If
// both resolve to the same value (e.g. both NA, the default combination),
// both conditions are correctly flagged as NoData. See ResolveRasterNoData
// below for the conflict-surfacing rule when both are given non-default,
// different, non-NA values.
struct SentinelPolicy {
    SentinelValue nodata{std::numeric_limits<float>::quiet_NaN(), true};   // /nodata, default NA
    SentinelValue noheight{0.0f, false};                                  // /noheight, default 0
};

// Picks the single NoData value a GDALRaster::Create call should register
// for a tool's output, given the resolved /nodata and /noheight sentinels
// plus whether each option was left at its default or explicitly set by
// the caller. GDAL supports only one registered NoData value per file (see
// GDALRaster::Create's single noDataValue parameter) -- when both sentinels
// resolve to the same float (bitwise, with NaN treated as equal to NaN for
// this purpose since two independently-computed quiet_NaN() values compare
// unequal under IEEE 754), there is no conflict. When they differ, /nodata
// always wins (preserving today's raster-NoData behavior), but if the
// caller explicitly set *both* options to different, non-default values,
// that's a real conflict the caller should be told about rather than have
// silently resolved -- conflict is set to true so the calling tool can
// print a warning naming which value it chose.
struct RasterNoDataResolution {
    float value;
    bool conflict; // true iff both /nodata and /noheight were explicitly set to different, non-NA values
};

inline RasterNoDataResolution ResolveRasterNoData(const SentinelPolicy& policy, bool nodataExplicit, bool noheightExplicit) {
    // Only the "two different non-NA literals" case (e.g. /nodata:-9999
    // and /noheight:-8888) is a real which-value-wins conflict worth
    // warning about -- if either side is NA, there's no ambiguity: NA can
    // only ever come from /nodata (noheight's value simply becomes a plain
    // pixel, per the GDAL raster caveat), so that combination is the clean,
    // documented default behavior, not a conflict.
    bool conflict = nodataExplicit && noheightExplicit &&
                     !policy.nodata.isNA && !policy.noheight.isNA &&
                     policy.nodata.value != policy.noheight.value;
    return {policy.nodata.value, conflict};
}

} // namespace fusion::metrics

#endif // FUSION_METRICS_SENTINELPOLICY_H

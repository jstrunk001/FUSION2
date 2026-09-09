#ifndef FUSION_METRICS_SPECTRALCHANNELS_H
#define FUSION_METRICS_SPECTRALCHANNELS_H

#include "fusion/lidar/LASPointCloud.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fusion::metrics {

// PointRecord only carries four spectral fields today, so --rgb: is scoped
// to those four -- a future point-cloud format with more bands becomes a
// matter of adding one field to PointRecord and one row here, not
// redesigning the option or the accumulation/output code that consumes it.
struct SpectralChannelSpec {
    std::string token;   // "R", "G", "B", "N" -- what --rgb: accepts
    std::string prefix;  // "red", "green", "blue", "nir" -- output column/band prefix
    uint16_t fusion::lidar::PointRecord::* field;
};
extern const std::vector<SpectralChannelSpec> kKnownSpectralChannels;

// True if the given LAS point format actually populates RGB / NIR fields
// (other formats leave PointRecord's red/green/blue/nir at their {0}
// default): RGB in formats 2, 3, 5, 7, 8, 10; NIR in formats 8 and 10 only.
bool PointFormatHasRGB(uint8_t pointFormat);
bool PointFormatHasNIR(uint8_t pointFormat);

// Parses "R,G,B", "N", or "all"/"ALL" (case-insensitive) into the matching
// subset of kKnownSpectralChannels, filtered to those the given pointFormat
// actually carries. A token that isn't a known channel letter, or that is
// known but not carried by this pointFormat, goes into unknownTokens
// instead of being silently dropped -- callers should print a warning
// naming each one (and the file's pointFormat) rather than fail the run.
struct SpectralChannelSelection {
    std::vector<SpectralChannelSpec> channels;
    std::vector<std::string> unknownTokens;
};
SpectralChannelSelection ParseSpectralChannels(const std::string& raw, uint8_t pointFormat);

} // namespace fusion::metrics

#endif // FUSION_METRICS_SPECTRALCHANNELS_H

#include "fusion/metrics/SpectralChannels.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace fusion::metrics {

const std::vector<SpectralChannelSpec> kKnownSpectralChannels = {
    {"R", "red", &fusion::lidar::PointRecord::red},
    {"G", "green", &fusion::lidar::PointRecord::green},
    {"B", "blue", &fusion::lidar::PointRecord::blue},
    {"N", "nir", &fusion::lidar::PointRecord::nir},
};

bool PointFormatHasRGB(uint8_t pointFormat) {
    return pointFormat == 2 || pointFormat == 3 || pointFormat == 5 ||
           pointFormat == 7 || pointFormat == 8 || pointFormat == 10;
}

bool PointFormatHasNIR(uint8_t pointFormat) {
    return pointFormat == 8 || pointFormat == 10;
}

namespace {
bool ChannelCarriedByFormat(const std::string& token, uint8_t pointFormat) {
    if (token == "N") {
        return PointFormatHasNIR(pointFormat);
    }
    return PointFormatHasRGB(pointFormat);
}
} // namespace

SpectralChannelSelection ParseSpectralChannels(const std::string& raw, uint8_t pointFormat) {
    SpectralChannelSelection result;

    std::string upper = raw;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                    [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    std::vector<std::string> tokens;
    if (upper == "ALL") {
        for (const auto& spec : kKnownSpectralChannels) {
            tokens.push_back(spec.token);
        }
    } else {
        std::stringstream ss(upper);
        std::string item;
        while (std::getline(ss, item, ',')) {
            if (!item.empty()) {
                tokens.push_back(item);
            }
        }
    }

    for (const auto& token : tokens) {
        auto it = std::find_if(kKnownSpectralChannels.begin(), kKnownSpectralChannels.end(),
                                [&](const SpectralChannelSpec& spec) { return spec.token == token; });
        if (it == kKnownSpectralChannels.end() || !ChannelCarriedByFormat(token, pointFormat)) {
            result.unknownTokens.push_back(token);
            continue;
        }
        result.channels.push_back(*it);
    }

    return result;
}

} // namespace fusion::metrics

#ifndef FUSION_CLI_ZTRANSFORM_H
#define FUSION_CLI_ZTRANSFORM_H

#include "fusion/cli/ArgumentParser.h"

#include <cmath>
#include <string>
#include <vector>

namespace fusion::cli {

// Shared vertical scale and offset (/zmult, /zshift) applied to final
// elevation or height-above-ground values: z_out = z * mult + shift.
// Centralizes option registration and parsing so every tool exposes identical
// /zmult and /zshift semantics.
struct ZTransform {
    double mult{1.0};
    double shift{0.0};

    static void RegisterOptions(ArgumentParser& parser) {
        parser.AddOption("zmult",
            "Multiply final elevation/height values by this factor (e.g. 0.3048 for feet to meters); height/elevation thresholds apply after this transform",
            "1.0");
        parser.AddOption("zshift",
            "Add this offset to final elevation/height values after /zmult; height/elevation thresholds apply after this transform",
            "0.0");
    }

    static ZTransform FromParser(const ArgumentParser& parser) {
        ZTransform t;
        if (auto m = parser.GetOption("zmult")) {
            t.mult = std::stod(*m);
        }
        if (auto s = parser.GetOption("zshift")) {
            t.shift = std::stod(*s);
        }
        return t;
    }

    static void RegisterGroundOptions(ArgumentParser& parser) {
        parser.AddOption("zmult_ground",
            "Multiply /ground raster elevation values by this factor before subtracting from point Z (e.g. 3.28084 for meters to feet)",
            "1.0");
        parser.AddOption("zshift_ground",
            "Add this offset to /ground raster elevation values after /zmult_ground, before subtracting from point Z",
            "0.0");
    }

    static ZTransform FromGroundParser(const ArgumentParser& parser) {
        ZTransform t;
        if (auto m = parser.GetOption("zmult_ground")) {
            t.mult = std::stod(*m);
        }
        if (auto s = parser.GetOption("zshift_ground")) {
            t.shift = std::stod(*s);
        }
        return t;
    }

    bool IsIdentity() const {
        return mult == 1.0 && shift == 0.0;
    }

    double Apply(double z) const {
        return z * mult + shift;
    }

    void ApplyToRaster(std::vector<float>& values, double noData) const {
        if (IsIdentity()) return;
        const float noDataF = static_cast<float>(noData);
        for (float& v : values) {
            if (v != noDataF && !std::isnan(v)) {
                v = static_cast<float>(Apply(static_cast<double>(v)));
            }
        }
    }
};

} // namespace fusion::cli

#endif // FUSION_CLI_ZTRANSFORM_H

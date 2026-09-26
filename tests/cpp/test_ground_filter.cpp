#include "fusion/lidar/GroundFilter.h"
#include "test_assert.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

// Checks the Kraus & Pfeifer weight function, the empty-cell fill, and the
// filter itself on a synthetic scene: a sloped ground plane whose returns
// are sparse under a dense block of tall canopy, the case a plain
// lowest-return-per-cell grid gets wrong.

namespace {

double GroundAt(double x, double y) {
    return 100.0 + 0.10 * x + 0.05 * y;
}

} // namespace

int RunGroundFilterTests() {
    int failures = 0;
    std::cout << "GroundFilter (Kraus & Pfeifer) tests\n";

    // Weight function: full weight at or below g, zero above g + w, and
    // 1 / (1 + (a (v - g))^b) in between.
    {
        fusion::lidar::KrausPfeiferParams p; // g -2, w 2.5, a 1, b 4
        CHECK(fusion::lidar::KrausPfeiferWeight(-5.0, p) == 1.0, failures);
        CHECK(fusion::lidar::KrausPfeiferWeight(-2.0, p) == 1.0, failures);
        CHECK(fusion::lidar::KrausPfeiferWeight(0.6, p) == 0.0, failures);
        CHECK(std::abs(fusion::lidar::KrausPfeiferWeight(-1.0, p) - 0.5) < 1e-12, failures);
    }

    // FillEmptyCells: a single filled cell spreads to the whole grid, and a
    // hole surrounded by equal values is filled with that value.
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        std::vector<double> one(5 * 4, nan);
        one[7] = 42.0;
        fusion::lidar::FillEmptyCells(one, 5, 4);
        bool allFilled = true;
        for (double v : one) allFilled = allFilled && std::abs(v - 42.0) < 1e-12;
        CHECK(allFilled, failures);

        std::vector<double> hole(3 * 3, 7.0);
        hole[4] = nan;
        fusion::lidar::FillEmptyCells(hole, 3, 3);
        CHECK(std::abs(hole[4] - 7.0) < 1e-12, failures);

        std::vector<double> empty(4, nan);
        fusion::lidar::FillEmptyCells(empty, 2, 2);
        CHECK(std::isnan(empty[0]), failures);
    }

    // Classification on a 200 x 200 scene: ground returns every 1 unit in
    // the open, every 4 units under a 100 x 100 canopy block that also has
    // canopy returns every 1 unit at 30-100 units above ground.
    {
        std::vector<double> x, y, z;
        std::vector<uint8_t> truth;
        for (int i = 0; i < 200; ++i) {
            for (int j = 0; j < 200; ++j) {
                double px = i + 0.5;
                double py = j + 0.5;
                bool underCanopy = (i >= 50 && i < 150 && j >= 50 && j < 150);
                if (!underCanopy || (i % 4 == 0 && j % 4 == 0)) {
                    x.push_back(px);
                    y.push_back(py);
                    z.push_back(GroundAt(px, py));
                    truth.push_back(1);
                }
                if (underCanopy) {
                    double height = 30.0 + static_cast<double>((i * 7 + j * 13) % 71);
                    x.push_back(px);
                    y.push_back(py);
                    z.push_back(GroundAt(px, py) + height);
                    truth.push_back(0);
                }
            }
        }

        fusion::lidar::KrausPfeiferParams p;
        p.cellSize = 10.0;
        p.iterations = 10;
        auto isGround = fusion::lidar::ClassifyGroundKrausPfeifer(x, y, z, 0.0, 0.0, 200.0, 200.0, p);
        CHECK(isGround.size() == z.size(), failures);

        size_t groundTotal = 0, groundKept = 0, canopyTotal = 0, canopyKept = 0;
        size_t underCanopyGround = 0, underCanopyGroundKept = 0;
        for (size_t k = 0; k < isGround.size() && k < truth.size(); ++k) {
            if (truth[k]) {
                groundTotal++;
                groundKept += isGround[k];
                bool under = x[k] >= 50.0 && x[k] < 150.0 && y[k] >= 50.0 && y[k] < 150.0;
                if (under) {
                    underCanopyGround++;
                    underCanopyGroundKept += isGround[k];
                }
            } else {
                canopyTotal++;
                canopyKept += isGround[k];
            }
        }
        std::cout << "  ground kept " << groundKept << "/" << groundTotal
                  << " (under canopy " << underCanopyGroundKept << "/" << underCanopyGround << ")"
                  << ", canopy misclassified " << canopyKept << "/" << canopyTotal << "\n";
        CHECK(groundKept >= 0.98 * groundTotal, failures);
        CHECK(underCanopyGroundKept >= 0.95 * underCanopyGround, failures);
        CHECK(canopyKept == 0, failures);

        // Coarse-to-fine stage: a 60 x 60 crown with no ground returns at
        // all underneath is wider than the fine 30-unit neighbourhood, so
        // the fine passes alone settle on its lowest canopy returns; the
        // coarse stage (90-unit neighbourhoods) must exclude it.
        std::vector<double> gx, gy, gz;
        std::vector<uint8_t> gtruth;
        for (int i = 0; i < 200; ++i) {
            for (int j = 0; j < 200; ++j) {
                double px = i + 0.5;
                double py = j + 0.5;
                bool crown = (i >= 70 && i < 130 && j >= 70 && j < 130);
                gx.push_back(px);
                gy.push_back(py);
                if (crown) {
                    gz.push_back(GroundAt(px, py) + 60.0 + static_cast<double>((i * 7 + j * 13) % 31));
                    gtruth.push_back(0);
                } else {
                    gz.push_back(GroundAt(px, py));
                    gtruth.push_back(1);
                }
            }
        }
        auto fn_count = [&](const std::vector<uint8_t>& flags, size_t& groundKeptOut, size_t& canopyKeptOut) {
            groundKeptOut = 0;
            canopyKeptOut = 0;
            for (size_t k = 0; k < flags.size(); ++k) {
                if (gtruth[k]) groundKeptOut += flags[k];
                else canopyKeptOut += flags[k];
            }
        };
        size_t nGroundTruth = 0;
        for (uint8_t t : gtruth) nGroundTruth += t;

        fusion::lidar::KrausPfeiferParams fineOnly = p;
        fineOnly.coarseCellSize = 0.0;
        size_t fineGround = 0, fineCanopy = 0;
        fn_count(fusion::lidar::ClassifyGroundKrausPfeifer(gx, gy, gz, 0.0, 0.0, 200.0, 200.0, fineOnly), fineGround, fineCanopy);

        fusion::lidar::KrausPfeiferParams withCoarse = p;
        withCoarse.coarseCellSize = 30.0;
        size_t coarseGround = 0, coarseCanopy = 0;
        fn_count(fusion::lidar::ClassifyGroundKrausPfeifer(gx, gy, gz, 0.0, 0.0, 200.0, 200.0, withCoarse), coarseGround, coarseCanopy);

        std::cout << "  unsupported crown: fine only misclassifies " << fineCanopy
                  << " canopy points; with coarse stage " << coarseCanopy
                  << " (ground kept " << coarseGround << "/" << nGroundTruth << ")\n";
        CHECK(fineCanopy > 0, failures);   // documents why the coarse stage exists
        CHECK(coarseCanopy == 0, failures);
        CHECK(coarseGround >= 0.98 * nGroundTruth, failures);

        // Wrong-parameter guard: an empty input returns an empty result
        auto none = fusion::lidar::ClassifyGroundKrausPfeifer({}, {}, {}, 0.0, 0.0, 1.0, 1.0, p);
        CHECK(none.empty(), failures);
    }

    std::cout << (failures == 0 ? "  all passed\n" : ("  " + std::to_string(failures) + " failure(s)\n"));
    return failures;
}

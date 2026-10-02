#include "fusion/batch/StageRegistry.h"
#include "test_assert.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

bool HasArg(const std::vector<std::string>& args, const std::string& arg) {
    return std::find(args.begin(), args.end(), arg) != args.end();
}

bool HasArgStartingWith(const std::vector<std::string>& args, const std::string& prefix) {
    for (const auto& a : args) {
        if (a.rfind(prefix, 0) == 0) return true;
    }
    return false;
}

} // namespace

// pipeline.exe can only run a tool that has an entry in the stage
// registry. densitymetrics was missing from it, so a pipeline that listed
// densitymetrics failed even though the tool itself worked. These checks
// confirm the registry lists all 10 stages under unique names and that the
// densitymetrics entry builds the right command line and output name.
int RunStageRegistryTests() {
    int failures = 0;
    std::cout << "StageRegistry tests\n";

    //1. the registry holds 10 stages, each with a unique name that
    //   FindStage can look up
    const auto& stages = fusion::batch::GetStageRegistry();
    CHECK(stages.size() == 10, failures);
    std::set<std::string> names;
    for (const auto& s : stages) {
        names.insert(s.name);
        CHECK(fusion::batch::FindStage(s.name) != nullptr, failures);
    }
    CHECK(names.size() == stages.size(), failures);
    CHECK(fusion::batch::FindStage("not_a_tool") == nullptr, failures);

    //2. densitymetrics is registered as a point-cloud-to-raster stage that
    //   accepts a ground DEM
    const auto* dm = fusion::batch::FindStage("densitymetrics");
    CHECK(dm != nullptr, failures);
    if (dm == nullptr) return failures;
    CHECK(dm->exeName == "densitymetrics.exe", failures);
    CHECK(dm->inputKind == fusion::batch::ArtifactKind::PointCloud, failures);
    CHECK(dm->outputKind == fusion::batch::ArtifactKind::Raster, failures);
    CHECK(dm->acceptsGround, failures);

    //3. with a DEM from an earlier groundfilter stage, the command line
    //   uses that DEM (not a forwarded /ground value), writes to the output
    //   folder, and forwards the options densitymetrics understands
    std::filesystem::path input = std::filesystem::path("tiles") / "tile_0001.laz";
    std::filesystem::path outputPath = std::filesystem::path("out") / "densitymetrics" / "tile_0001.tif";
    std::filesystem::path dem = std::filesystem::path("out") / "groundfilter" / "tile_0001.tif";
    std::unordered_map<std::string, std::string> options = {
        {"cellsize", "5"}
      , {"strata", "0,2,10,30"}
      , {"class", "1,2"}
      , {"ground", "forwarded_dem.tif"}
      , {"iterations", "5"}
    };

    auto args = dm->buildArgs(input, dem, outputPath, options);
    CHECK(!args.empty() && args[0] == input.string(), failures);
    CHECK(HasArg(args, "/outdir:" + outputPath.parent_path().string()), failures);
    CHECK(HasArg(args, "/ground:" + dem.string()), failures);
    CHECK(!HasArg(args, "/ground:forwarded_dem.tif"), failures);
    CHECK(HasArg(args, "/cellsize:5"), failures);
    CHECK(HasArg(args, "/strata:0,2,10,30"), failures);
    CHECK(HasArg(args, "/class:1,2"), failures);
    CHECK(!HasArgStartingWith(args, "/iterations"), failures);

    //4. with no earlier DEM, a forwarded /ground value is passed through
    auto args_no_dem = dm->buildArgs(input, std::nullopt, outputPath, options);
    CHECK(HasArg(args_no_dem, "/ground:forwarded_dem.tif"), failures);

    //5. densitymetrics names its output from the input file's name, so the
    //   pipeline must look for <stem>_densitymetrics.tif in the output folder
    auto resolved = dm->resolveOutputPath(input, outputPath);
    CHECK(resolved == outputPath.parent_path() / "tile_0001_densitymetrics.tif", failures);

    //6. stage-chain checks
    //  - canopymaxima writes a table, so treeseg after it must still be
    //    allowed: pipeline hands treeseg the canopy model, the most recent
    //    raster in the chain (this chain was once rejected)
    //  - chains with no raster before a raster-input stage are still rejected
    std::string chainError;
    CHECK(fusion::batch::ValidateStageChain({"canopymodel", "canopymaxima", "treeseg"}, chainError), failures);
    CHECK(fusion::batch::ValidateStageChain({"canopymodel", "canopymaxima"}, chainError), failures);
    CHECK(fusion::batch::ValidateStageChain({"groundfilter", "canopymodel", "treeseg"}, chainError), failures);
    CHECK(!fusion::batch::ValidateStageChain({"canopymaxima"}, chainError), failures);
    CHECK(!fusion::batch::ValidateStageChain({"filterdata", "treeseg"}, chainError), failures);
    CHECK(!fusion::batch::ValidateStageChain({"canopymodel", "nosuchstage"}, chainError), failures);
    CHECK(!fusion::batch::ValidateStageChain({}, chainError), failures);

    //7. /gpu and /nogpu forwarding for canopymodel and gridmetrics
    const auto* cm = fusion::batch::FindStage("canopymodel");
    const auto* gm = fusion::batch::FindStage("gridmetrics");
    CHECK(cm != nullptr, failures);
    CHECK(gm != nullptr, failures);
    if (cm && gm) {
        std::unordered_map<std::string, std::string> gpuOpts = {{"gpu", "true"}};
        auto cmGpuArgs = cm->buildArgs(input, dem, outputPath, gpuOpts);
        auto gmGpuArgs = gm->buildArgs(input, dem, outputPath, gpuOpts);
        CHECK(HasArg(cmGpuArgs, "/gpu"), failures);
        CHECK(HasArg(gmGpuArgs, "/gpu"), failures);

        std::unordered_map<std::string, std::string> noGpuOpts = {{"nogpu", "true"}};
        auto cmNoGpuArgs = cm->buildArgs(input, dem, outputPath, noGpuOpts);
        auto gmNoGpuArgs = gm->buildArgs(input, dem, outputPath, noGpuOpts);
        CHECK(HasArg(cmNoGpuArgs, "/nogpu"), failures);
        CHECK(HasArg(gmNoGpuArgs, "/nogpu"), failures);
    }

    if (failures == 0) std::cout << "  all passed\n";
    return failures;
}

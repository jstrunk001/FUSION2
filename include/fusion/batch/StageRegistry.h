#ifndef FUSION_BATCH_STAGEREGISTRY_H
#define FUSION_BATCH_STAGEREGISTRY_H

#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <unordered_map>
#include <filesystem>

namespace fusion::batch {

enum class ArtifactKind {
    PointCloud,
    Raster,
    Table
};

// Describes one of the tools pipeline.exe can run per tile: which sibling
// .exe to launch, what kind of file it reads/writes, and how to translate
// pipeline.exe's forwarded options into that tool's own CLI arguments.
struct StageSpec {
    std::string name;      // "canopymodel" -- matches /pipeline: and /tool: values
    std::string exeName;   // "canopymodel.exe"
    ArtifactKind inputKind{ArtifactKind::PointCloud};
    ArtifactKind outputKind{ArtifactKind::Raster};
    std::string outputExt; // ".tif", ".laz", ".csv"

    // Whether this stage accepts a /ground option -- used for the auto-wiring
    // rule where an earlier groundfilter stage's DEM feeds later stages.
    bool acceptsGround{false};

    // Builds the full argument list for running this stage once on one tile.
    //   primaryInput     -- the point cloud or raster file this stage reads
    //   groundDemPath    -- an earlier groundfilter stage's DEM output, if
    //                       any and if this stage accepts /ground
    //   outputPath       -- the path pipeline.exe wants this stage's output
    //                       written to (most stages honor this exactly via
    //                       their own /output-style option)
    //   forwardedOptions -- every option value pipeline.exe was given, keyed
    //                       by name (e.g. "cellsize" -> "2.0"), so this stage
    //                       can pick out the subset it understands
    std::function<std::vector<std::string>(
        const std::filesystem::path& primaryInput,
        const std::optional<std::filesystem::path>& groundDemPath,
        const std::filesystem::path& outputPath,
        const std::unordered_map<std::string, std::string>& forwardedOptions)> buildArgs;

    // Resolves the actual path this stage will have written its output to,
    // given the same primaryInput/outputPath passed to buildArgs. Identity
    // for every stage except gridmetrics, whose single-file mode names its
    // own output from the input file's stem rather than honoring an exact
    // caller-given path.
    std::function<std::filesystem::path(
        const std::filesystem::path& primaryInput,
        const std::filesystem::path& outputPath)> resolveOutputPath;
};

// The 10 stages pipeline.exe can dispatch to: gridmetrics, canopymodel,
// groundfilter, returndensity, filterdata, thindata, canopymaxima,
// topometrics, treeseg, densitymetrics.
const std::vector<StageSpec>& GetStageRegistry();

// Looks up a stage by name (case-sensitive, matching /pipeline:/tool: values
// exactly); returns nullptr if name isn't one of the 10 registered stages.
const StageSpec* FindStage(const std::string& name);

// Validates a requested pipeline stage list: every name must be a
// registered stage, the first stage must consume a point cloud (the tile's
// buffered clip is the only thing available before any stage has run), and
// any stage that consumes a raster must have some earlier stage that
// produces one -- not necessarily the stage directly before it. pipeline
// feeds a raster-input stage the most recent raster in the chain, so
// canopymodel,canopymaxima,treeseg is valid: canopymaxima writes a table,
// and treeseg reads canopymodel's raster. Returns false with errorOut set
// when the list is invalid.
bool ValidateStageChain(const std::vector<std::string>& stageNames, std::string& errorOut);

} // namespace fusion::batch

#endif // FUSION_BATCH_STAGEREGISTRY_H

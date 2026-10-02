#include "fusion/batch/StageRegistry.h"

namespace fusion::batch {

// Forwards /name:<forwardedOptions[name]> into args, if the user passed it.
static void ForwardOption(std::vector<std::string>& args,
                           const std::unordered_map<std::string, std::string>& forwardedOptions,
                           const std::string& name) {
    auto it = forwardedOptions.find(name);
    if (it != forwardedOptions.end()) {
        args.push_back("/" + name + ":" + it->second);
    }
}

// Forwards a bare /name flag, if the user passed it (forwardedOptions stores
// flags as the literal value "true").
static void ForwardFlag(std::vector<std::string>& args,
                         const std::unordered_map<std::string, std::string>& forwardedOptions,
                         const std::string& name) {
    auto it = forwardedOptions.find(name);
    if (it != forwardedOptions.end() && it->second == "true") {
        args.push_back("/" + name);
    }
}

static std::filesystem::path IdentityOutputPath(const std::filesystem::path&, const std::filesystem::path& outputPath) {
    return outputPath;
}

const std::vector<StageSpec>& GetStageRegistry() {
    static const std::vector<StageSpec> registry = [] {
        std::vector<StageSpec> stages;

        // gridmetrics: single-file mode names its own output from the input
        // file's stem ("<stem>_gridmetrics.tif") into /outdir -- it has no
        // exact-filename /output option, so resolveOutputPath predicts that
        // name instead of taking outputPath at face value. Always forced to
        // multiband so exactly one raster comes out per tile, matching every
        // other stage's one-artifact-per-tile model (singleband mode writes
        // one file per band, which doesn't fit that model).
        {
            StageSpec s;
            s.name = "gridmetrics";
            s.exeName = "gridmetrics.exe";
            s.inputKind = ArtifactKind::PointCloud;
            s.outputKind = ArtifactKind::Raster;
            s.outputExt = ".tif";
            s.acceptsGround = true;
            s.buildArgs = [](const std::filesystem::path& primaryInput,
                              const std::optional<std::filesystem::path>& groundDemPath,
                              const std::filesystem::path& outputPath,
                              const std::unordered_map<std::string, std::string>& forwardedOptions) {
                std::vector<std::string> args;
                args.push_back(primaryInput.string());
                args.push_back("/outdir:" + outputPath.parent_path().string());
                args.push_back("/output-mode:multiband");
                if (groundDemPath) args.push_back("/ground:" + groundDemPath->string());
                else ForwardOption(args, forwardedOptions, "ground");
                ForwardOption(args, forwardedOptions, "cellsize");
                ForwardOption(args, forwardedOptions, "minht");
                ForwardOption(args, forwardedOptions, "heightcut");
                ForwardOption(args, forwardedOptions, "outlier");
                ForwardOption(args, forwardedOptions, "class");
                ForwardOption(args, forwardedOptions, "return");
                ForwardFlag(args, forwardedOptions, "first");
                ForwardFlag(args, forwardedOptions, "nointensity");
                ForwardFlag(args, forwardedOptions, "gpu");
                ForwardFlag(args, forwardedOptions, "nogpu");
                ForwardOption(args, forwardedOptions, "strata");
                ForwardOption(args, forwardedOptions, "intstrata");
                return args;
            };
            s.resolveOutputPath = [](const std::filesystem::path& primaryInput, const std::filesystem::path& outputPath) {
                return outputPath.parent_path() / (primaryInput.stem().string() + "_gridmetrics.tif");
            };
            stages.push_back(s);
        }

        // densitymetrics: LAS/LAZ -> height-stratified return-density raster stack.
        // Names its output from the input file's stem ("<stem>_densitymetrics.tif")
        // into /outdir, exactly like gridmetrics.
        {
            StageSpec s;
            s.name = "densitymetrics";
            s.exeName = "densitymetrics.exe";
            s.inputKind = ArtifactKind::PointCloud;
            s.outputKind = ArtifactKind::Raster;
            s.outputExt = ".tif";
            s.acceptsGround = true;
            s.buildArgs = [](const std::filesystem::path& primaryInput,
                              const std::optional<std::filesystem::path>& groundDemPath,
                              const std::filesystem::path& outputPath,
                              const std::unordered_map<std::string, std::string>& forwardedOptions) {
                std::vector<std::string> args;
                args.push_back(primaryInput.string());
                args.push_back("/outdir:" + outputPath.parent_path().string());
                if (groundDemPath) args.push_back("/ground:" + groundDemPath->string());
                else ForwardOption(args, forwardedOptions, "ground");
                ForwardOption(args, forwardedOptions, "cellsize");
                ForwardOption(args, forwardedOptions, "strata");
                ForwardOption(args, forwardedOptions, "class");
                ForwardOption(args, forwardedOptions, "return");
                ForwardOption(args, forwardedOptions, "nodata");
                return args;
            };
            s.resolveOutputPath = [](const std::filesystem::path& primaryInput, const std::filesystem::path& outputPath) {
                return outputPath.parent_path() / (primaryInput.stem().string() + "_densitymetrics.tif");
            };
            stages.push_back(s);
        }

        // canopymodel: LAS/LAZ -> CHM raster.
        {
            StageSpec s;
            s.name = "canopymodel";
            s.exeName = "canopymodel.exe";
            s.inputKind = ArtifactKind::PointCloud;
            s.outputKind = ArtifactKind::Raster;
            s.outputExt = ".tif";
            s.acceptsGround = true;
            s.buildArgs = [](const std::filesystem::path& primaryInput,
                              const std::optional<std::filesystem::path>& groundDemPath,
                              const std::filesystem::path& outputPath,
                              const std::unordered_map<std::string, std::string>& forwardedOptions) {
                std::vector<std::string> args;
                args.push_back(primaryInput.string());
                args.push_back("/output:" + outputPath.string());
                if (groundDemPath) args.push_back("/ground:" + groundDemPath->string());
                else ForwardOption(args, forwardedOptions, "ground");
                ForwardOption(args, forwardedOptions, "cellsize");
                ForwardFlag(args, forwardedOptions, "slope");
                ForwardFlag(args, forwardedOptions, "gpu");
                ForwardFlag(args, forwardedOptions, "nogpu");
                ForwardOption(args, forwardedOptions, "smooth");
                ForwardOption(args, forwardedOptions, "class");
                ForwardOption(args, forwardedOptions, "return");
                return args;
            };
            s.resolveOutputPath = IdentityOutputPath;
            stages.push_back(s);
        }

        // groundfilter: LAS/LAZ -> ground DEM raster.
        {
            StageSpec s;
            s.name = "groundfilter";
            s.exeName = "groundfilter.exe";
            s.inputKind = ArtifactKind::PointCloud;
            s.outputKind = ArtifactKind::Raster;
            s.outputExt = ".tif";
            s.acceptsGround = false;
            s.buildArgs = [](const std::filesystem::path& primaryInput,
                              const std::optional<std::filesystem::path>&,
                              const std::filesystem::path& outputPath,
                              const std::unordered_map<std::string, std::string>& forwardedOptions) {
                std::vector<std::string> args;
                args.push_back(primaryInput.string());
                args.push_back("/output-raster:" + outputPath.string());
                ForwardOption(args, forwardedOptions, "cellsize");
                ForwardOption(args, forwardedOptions, "class");
                ForwardOption(args, forwardedOptions, "return");
                return args;
            };
            s.resolveOutputPath = IdentityOutputPath;
            stages.push_back(s);
        }

        // returndensity: LAS/LAZ -> point density/return-ratio raster.
        {
            StageSpec s;
            s.name = "returndensity";
            s.exeName = "returndensity.exe";
            s.inputKind = ArtifactKind::PointCloud;
            s.outputKind = ArtifactKind::Raster;
            s.outputExt = ".tif";
            s.buildArgs = [](const std::filesystem::path& primaryInput,
                              const std::optional<std::filesystem::path>&,
                              const std::filesystem::path& outputPath,
                              const std::unordered_map<std::string, std::string>& forwardedOptions) {
                std::vector<std::string> args;
                args.push_back(primaryInput.string());
                args.push_back("/output:" + outputPath.string());
                ForwardOption(args, forwardedOptions, "cellsize");
                return args;
            };
            s.resolveOutputPath = IdentityOutputPath;
            stages.push_back(s);
        }

        // filterdata: LAS/LAZ -> filtered LAS/LAZ.
        {
            StageSpec s;
            s.name = "filterdata";
            s.exeName = "filterdata.exe";
            s.inputKind = ArtifactKind::PointCloud;
            s.outputKind = ArtifactKind::PointCloud;
            s.outputExt = ".laz";
            s.buildArgs = [](const std::filesystem::path& primaryInput,
                              const std::optional<std::filesystem::path>&,
                              const std::filesystem::path& outputPath,
                              const std::unordered_map<std::string, std::string>& forwardedOptions) {
                std::vector<std::string> args;
                args.push_back(primaryInput.string());
                args.push_back("/output:" + outputPath.string());
                ForwardOption(args, forwardedOptions, "minz");
                ForwardOption(args, forwardedOptions, "maxz");
                ForwardOption(args, forwardedOptions, "return");
                ForwardOption(args, forwardedOptions, "class");
                return args;
            };
            s.resolveOutputPath = IdentityOutputPath;
            stages.push_back(s);
        }

        // thindata: LAS/LAZ -> spatially thinned LAS/LAZ.
        {
            StageSpec s;
            s.name = "thindata";
            s.exeName = "thindata.exe";
            s.inputKind = ArtifactKind::PointCloud;
            s.outputKind = ArtifactKind::PointCloud;
            s.outputExt = ".laz";
            s.buildArgs = [](const std::filesystem::path& primaryInput,
                              const std::optional<std::filesystem::path>&,
                              const std::filesystem::path& outputPath,
                              const std::unordered_map<std::string, std::string>& forwardedOptions) {
                std::vector<std::string> args;
                args.push_back(primaryInput.string());
                args.push_back("/output:" + outputPath.string());
                ForwardOption(args, forwardedOptions, "cellsize");
                ForwardOption(args, forwardedOptions, "class");
                ForwardOption(args, forwardedOptions, "return");
                return args;
            };
            s.resolveOutputPath = IdentityOutputPath;
            stages.push_back(s);
        }

        // canopymaxima: CHM raster -> tree-top table (CSV).
        {
            StageSpec s;
            s.name = "canopymaxima";
            s.exeName = "canopymaxima.exe";
            s.inputKind = ArtifactKind::Raster;
            s.outputKind = ArtifactKind::Table;
            s.outputExt = ".csv";
            s.buildArgs = [](const std::filesystem::path& primaryInput,
                              const std::optional<std::filesystem::path>&,
                              const std::filesystem::path& outputPath,
                              const std::unordered_map<std::string, std::string>& forwardedOptions) {
                std::vector<std::string> args;
                args.push_back(primaryInput.string());
                args.push_back("/output:" + outputPath.string());
                ForwardOption(args, forwardedOptions, "minht");
                ForwardOption(args, forwardedOptions, "window-a");
                ForwardOption(args, forwardedOptions, "window-b");
                return args;
            };
            s.resolveOutputPath = IdentityOutputPath;
            stages.push_back(s);
        }

        // topometrics: DEM raster -> slope/aspect raster.
        {
            StageSpec s;
            s.name = "topometrics";
            s.exeName = "topometrics.exe";
            s.inputKind = ArtifactKind::Raster;
            s.outputKind = ArtifactKind::Raster;
            s.outputExt = ".tif";
            s.buildArgs = [](const std::filesystem::path& primaryInput,
                              const std::optional<std::filesystem::path>&,
                              const std::filesystem::path& outputPath,
                              const std::unordered_map<std::string, std::string>&) {
                std::vector<std::string> args;
                args.push_back(primaryInput.string());
                args.push_back("/output:" + outputPath.string());
                return args;
            };
            s.resolveOutputPath = IdentityOutputPath;
            stages.push_back(s);
        }

        // treeseg: CHM raster -> crown-segment raster + crown-summary table.
        // The table's path is derived from the raster outputPath (same stem,
        // .csv extension) -- finalization special-cases this stage to also
        // concatenate that sibling table, since ArtifactKind models one
        // primary output per stage and treeseg genuinely produces two.
        {
            StageSpec s;
            s.name = "treeseg";
            s.exeName = "treeseg.exe";
            s.inputKind = ArtifactKind::Raster;
            s.outputKind = ArtifactKind::Raster;
            s.outputExt = ".tif";
            s.buildArgs = [](const std::filesystem::path& primaryInput,
                              const std::optional<std::filesystem::path>&,
                              const std::filesystem::path& outputPath,
                              const std::unordered_map<std::string, std::string>& forwardedOptions) {
                std::vector<std::string> args;
                args.push_back(primaryInput.string());
                args.push_back("/output-raster:" + outputPath.string());
                std::filesystem::path tablePath = outputPath;
                tablePath.replace_extension(".csv");
                args.push_back("/output-table:" + tablePath.string());
                ForwardOption(args, forwardedOptions, "minht");
                return args;
            };
            s.resolveOutputPath = IdentityOutputPath;
            stages.push_back(s);
        }

        return stages;
    }();

    return registry;
}

const StageSpec* FindStage(const std::string& name) {
    for (const auto& stage : GetStageRegistry()) {
        if (stage.name == name) return &stage;
    }
    return nullptr;
}

bool ValidateStageChain(const std::vector<std::string>& stageNames, std::string& errorOut) {
    if (stageNames.empty()) {
        errorOut = "Pipeline stage list is empty.";
        return false;
    }

    std::vector<const StageSpec*> stages;
    for (const auto& name : stageNames) {
        const StageSpec* spec = fusion::batch::FindStage(name);
        if (!spec) {
            errorOut = "Unknown stage \"" + name + "\". Valid stages: gridmetrics, densitymetrics, canopymodel, "
                       "groundfilter, returndensity, filterdata, thindata, canopymaxima, topometrics, treeseg.";
            return false;
        }
        stages.push_back(spec);
    }

    if (stages.front()->inputKind != ArtifactKind::PointCloud) {
        errorOut = "The first pipeline stage must consume a point cloud (gridmetrics, densitymetrics, canopymodel, "
                   "groundfilter, returndensity, filterdata, or thindata) -- \"" + stages.front()->name +
                   "\" expects a raster as input, and no raster exists yet at the start of a tile.";
        return false;
    }

    // a raster-input stage needs some earlier stage that produced a raster
    bool rasterAvailable = stages.front()->outputKind == ArtifactKind::Raster;
    for (size_t i = 1; i < stages.size(); ++i) {
        if (stages[i]->inputKind == ArtifactKind::Raster && !rasterAvailable) {
            errorOut = "\"" + stages[i]->name + "\" expects a raster input, but no earlier stage produces one.";
            return false;
        }
        if (stages[i]->outputKind == ArtifactKind::Raster) rasterAvailable = true;
    }

    return true;
}

} // namespace fusion::batch

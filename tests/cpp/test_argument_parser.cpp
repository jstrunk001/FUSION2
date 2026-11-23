#include "fusion/cli/ArgumentParser.h"
#include "fusion/cli/ZTransform.h"
#include "test_assert.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

// Parses a command line given as strings, the way main() receives it
// (argv[0] is the program name).
fusion::cli::ArgumentParser ParseLine(const std::vector<std::string>& args) {
    fusion::cli::ArgumentParser parser("thindata", "test");
    parser.AddOption("output", "output path", "thinned_output.laz");
    parser.AddOption("cellsize", "cell size", "1.0");
    parser.AddFlag("first", "first returns only");

    std::vector<std::string> storage = args;
    storage.insert(storage.begin(), "thindata");
    std::vector<char*> argv;
    for (auto& s : storage) argv.push_back(s.data());
    parser.Parse(static_cast<int>(argv.size()), argv.data());
    return parser;
}

} // namespace

// Every tool accepts options three ways: /name:value, -name value, and
// --name=value. The --name=value form used to keep one of its two dashes
// in the option name ("-output"), so no tool ever found the option and
// each silently fell back to its defaults. These checks confirm all three
// forms set the same options and flags.
int RunArgumentParserTests() {
    int failures = 0;
    std::cout << "ArgumentParser tests\n";

    //1. the three option syntaxes give the same values
    auto slash = ParseLine({"tile.laz", "/output:out.las", "/cellsize:2", "/first"});
    auto dash = ParseLine({"tile.laz", "-output", "out.las", "-cellsize", "2", "-first"});
    auto double_dash = ParseLine({"tile.laz", "--output=out.las", "--cellsize=2", "--first"});
    for (const auto* parser : {&slash, &dash, &double_dash}) {
        CHECK(parser->GetOption("output") == std::optional<std::string>("out.las"), failures);
        CHECK(parser->GetOption("cellsize") == std::optional<std::string>("2"), failures);
        CHECK(parser->HasFlag("first"), failures);
        CHECK(parser->GetPositionalArgs().size() == 1 && parser->GetPositionalArgs()[0] == "tile.laz", failures);
    }

    //2. an option that isn't given falls back to its registered default
    auto defaults = ParseLine({"tile.laz"});
    CHECK(defaults.GetOption("output") == std::optional<std::string>("thinned_output.laz"), failures);
    CHECK(!defaults.HasFlag("first"), failures);

    //3. option names are case-insensitive
    auto upper = ParseLine({"tile.laz", "/OUTPUT:out.las"});
    CHECK(upper.GetOption("output") == std::optional<std::string>("out.las"), failures);

    //4. flag parsing for /gpu and /nogpu
    {
        fusion::cli::ArgumentParser cmParser("canopymodel", "test");
        cmParser.AddFlag("gpu", "enable GPU acceleration");
        cmParser.AddFlag("nogpu", "disable GPU acceleration");
        std::vector<std::string> args = {"canopymodel", "input.laz", "/gpu"};
        std::vector<char*> argv;
        for (auto& s : args) argv.push_back(s.data());
        cmParser.Parse(static_cast<int>(argv.size()), argv.data());
        CHECK(cmParser.HasFlag("gpu"), failures);
        CHECK(!cmParser.HasFlag("nogpu"), failures);
        CHECK(cmParser.GetPositionalArgs().size() == 1, failures);
        CHECK(cmParser.GetPositionalArgs()[0] == "input.laz", failures);
    }

    //5. flag parsing for /profile and raster positional inputs
    {
        fusion::cli::ArgumentParser gmParser("gridmetrics", "test");
        gmParser.AddFlag("profile", "execution profiling");
        std::vector<std::string> args = {"gridmetrics", "dsm.tif", "ground.tif", "/profile"};
        std::vector<char*> argv;
        for (auto& s : args) argv.push_back(s.data());
        gmParser.Parse(static_cast<int>(argv.size()), argv.data());
        CHECK(gmParser.HasFlag("profile"), failures);
        CHECK(gmParser.GetPositionalArgs().size() == 2, failures);
        CHECK(gmParser.GetPositionalArgs()[0] == "dsm.tif", failures);
        CHECK(gmParser.GetPositionalArgs()[1] == "ground.tif", failures);
    }

    //6. ZTransform defaults, parsing, and raster application
    {
        fusion::cli::ArgumentParser ztDefaultParser("tool", "test");
        fusion::cli::ZTransform::RegisterOptions(ztDefaultParser);
        std::vector<std::string> args0 = {"tool", "in.laz"};
        std::vector<char*> argv0;
        for (auto& s : args0) argv0.push_back(s.data());
        ztDefaultParser.Parse(static_cast<int>(argv0.size()), argv0.data());
        auto zt0 = fusion::cli::ZTransform::FromParser(ztDefaultParser);
        CHECK(zt0.IsIdentity(), failures);
        CHECK(zt0.Apply(10.0) == 10.0, failures);

        fusion::cli::ArgumentParser ztParser("tool", "test");
        fusion::cli::ZTransform::RegisterOptions(ztParser);
        std::vector<std::string> args1 = {"tool", "in.laz", "/zmult:0.5", "/zshift:2.0"};
        std::vector<char*> argv1;
        for (auto& s : args1) argv1.push_back(s.data());
        ztParser.Parse(static_cast<int>(argv1.size()), argv1.data());
        auto zt1 = fusion::cli::ZTransform::FromParser(ztParser);
        CHECK(!zt1.IsIdentity(), failures);
        CHECK(zt1.Apply(10.0) == 7.0, failures);

        std::vector<float> raster = {10.0f, -9999.0f, std::numeric_limits<float>::quiet_NaN(), 20.0f};
        zt1.ApplyToRaster(raster, -9999.0);
        CHECK(raster[0] == 7.0f, failures);
        CHECK(raster[1] == -9999.0f, failures);
        CHECK(std::isnan(raster[2]), failures);
        CHECK(raster[3] == 12.0f, failures);

        fusion::cli::ArgumentParser ztgParser("tool", "test");
        fusion::cli::ZTransform::RegisterGroundOptions(ztgParser);
        std::vector<std::string> args2 = {"tool", "in.laz", "/zmult_ground:3.28084", "/zshift_ground:1.5"};
        std::vector<char*> argv2;
        for (auto& s : args2) argv2.push_back(s.data());
        ztgParser.Parse(static_cast<int>(argv2.size()), argv2.data());
        auto ztg = fusion::cli::ZTransform::FromGroundParser(ztgParser);
        CHECK(!ztg.IsIdentity(), failures);
        CHECK(std::abs(ztg.Apply(10.0) - 34.3084) < 1e-6, failures);
    }

    if (failures == 0) std::cout << "  all passed\n";
    return failures;
}

#include <iostream>

int RunPointFilterTests();
int RunStrataStatBundleTests();
int RunGetModeTests();
int RunLASWriterTests();
int RunChmSmoothingTests();
int RunTableWriterTests();
int RunGroundFilterTests();
int RunRasterSamplingTests();
int RunSurfaceStatsTests();
int RunStageRegistryTests();
int RunArgumentParserTests();
int RunPointDensityTests();
int RunTileCellTests();
int RunProjectExtentTests();

int main() {
    int failures = 0;
    failures += RunPointFilterTests();
    failures += RunStrataStatBundleTests();
    failures += RunGetModeTests();
    failures += RunLASWriterTests();
    failures += RunChmSmoothingTests();
    failures += RunTableWriterTests();
    failures += RunGroundFilterTests();
    failures += RunRasterSamplingTests();
    failures += RunSurfaceStatsTests();
    failures += RunStageRegistryTests();
    failures += RunArgumentParserTests();
    failures += RunPointDensityTests();
    failures += RunTileCellTests();
    failures += RunProjectExtentTests();

    if (failures == 0) {
        std::cout << "\nAll fusion_tests passed.\n";
    } else {
        std::cout << "\n" << failures << " fusion_tests failure(s).\n";
    }
    return failures == 0 ? 0 : 1;
}

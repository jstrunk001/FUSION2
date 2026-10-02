#include "fusion/batch/PipelineState.h"
#include "test_assert.h"

#include <iostream>
#include <string>
#include <vector>

// pipeline joins each table stage's per-tile tables in the order
// PipelineState::ResultsForStage lists the tiles. That order was once the
// order tiles finished, so canopymaxima_all.csv and treeseg_table_all.csv
// held the same rows in a different order on every run. These checks
// confirm results come back in tile-number order however they were recorded.
int RunPipelineStateTests() {
    int failures = 0;
    std::cout << "PipelineState tests\n";

    //1. record results out of tile order, across two stages
    //  - no Load() call, so nothing is written to disk
    fusion::batch::PipelineState state;
    std::vector<std::string> finishOrder = {"tile_0010", "tile_0002", "tile_10000", "tile_0001", "tile_9999"};
    for (const auto& tile : finishOrder) {
        fusion::batch::StageResult result;
        result.tile = tile;
        result.stage = "canopymaxima";
        result.status = fusion::batch::StageStatus::Done;
        state.Record(result);
        result.stage = "canopymodel";
        state.Record(result);
    }

    //2. one stage's results come back in tile-number order
    //  - tile_10000 after tile_9999, which a plain text sort would reverse
    auto results = state.ResultsForStage("canopymaxima");
    std::vector<std::string> tiles;
    for (const auto& r : results) tiles.push_back(r.tile);
    std::vector<std::string> expected = {"tile_0001", "tile_0002", "tile_0010", "tile_9999", "tile_10000"};
    CHECK(tiles == expected, failures);

    //3. only the requested stage is returned
    bool allCanopymaxima = true;
    for (const auto& r : results) {
        if (r.stage != "canopymaxima") allCanopymaxima = false;
    }
    CHECK(allCanopymaxima, failures);
    CHECK(state.ResultsForStage("treeseg").empty(), failures);

    std::cout << (failures == 0 ? "  all passed\n" : ("  " + std::to_string(failures) + " failure(s)\n"));
    return failures;
}

#ifndef FUSION_BATCH_PIPELINESTATE_H
#define FUSION_BATCH_PIPELINESTATE_H

#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <filesystem>

namespace fusion::batch {

enum class StageStatus {
    Done,
    Failed
};

struct StageResult {
    std::string tile;
    std::string stage;
    StageStatus status{StageStatus::Failed};
    std::string outputPath;
    int exitCode{0};
    std::string startedAt;
    std::string finishedAt;
};

// Reads and writes the pipeline's tile x stage state manifest
// (_processing/pipeline_state.csv). This is the single source of truth for
// resume checks during a run and for per-stage finalization (VRT/merge,
// CSV concatenation) afterward -- pipeline.exe keeps no separate in-memory
// record of what each tile produced.
class PipelineState {
public:
    // Loads an existing manifest at manifestPath, if present; safe to call
    // when the file doesn't exist yet (starts empty). Remembers manifestPath
    // for subsequent Record() calls.
    bool Load(const std::filesystem::path& manifestPath);

    // Looks up the recorded result for (tile, stage); returns nullptr if none
    // is recorded yet.
    const StageResult* Find(const std::string& tile, const std::string& stage) const;

    // Records a result, replacing any prior entry for the same (tile, stage),
    // and appends the manifest file on disk. Thread-safe -- called from
    // multiple tile worker threads.
    void Record(const StageResult& result);

    // Tile names that have at least one stage recorded with the given status.
    std::vector<std::string> TilesWithStatus(StageStatus status) const;

    // All recorded results for a given stage name, in tile-number order
    // (tile_0001, tile_0002, ...), so per-stage finalization concatenates
    // tables in the same order on every run, whatever order tiles finished.
    std::vector<StageResult> ResultsForStage(const std::string& stage) const;

private:
    void RewriteManifestFile() const;

    std::filesystem::path m_manifestPath;
    mutable std::mutex m_mutex;
    std::vector<StageResult> m_results;
    std::unordered_map<std::string, size_t> m_index; // "tile|stage" -> index into m_results
};

} // namespace fusion::batch

#endif // FUSION_BATCH_PIPELINESTATE_H

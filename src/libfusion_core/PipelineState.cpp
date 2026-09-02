#include "fusion/batch/PipelineState.h"

#include <fstream>
#include <sstream>
#include <algorithm>

namespace fusion::batch {

static std::string StatusToString(StageStatus status) {
    return status == StageStatus::Done ? "done" : "failed";
}

static StageStatus StatusFromString(const std::string& str) {
    return str == "done" ? StageStatus::Done : StageStatus::Failed;
}

static std::string Key(const std::string& tile, const std::string& stage) {
    return tile + "|" + stage;
}

static std::vector<std::string> SplitCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::stringstream ss(line);
    std::string field;
    while (std::getline(ss, field, ',')) {
        fields.push_back(field);
    }
    return fields;
}

bool PipelineState::Load(const std::filesystem::path& manifestPath) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_manifestPath = manifestPath;
    m_results.clear();
    m_index.clear();

    std::ifstream in(manifestPath);
    if (!in.is_open()) {
        return true; // No prior manifest -- starts empty, not an error.
    }

    std::string line;
    bool first = true;
    while (std::getline(in, line)) {
        if (first) { // skip header row
            first = false;
            continue;
        }
        if (line.empty()) continue;

        auto fields = SplitCsvLine(line);
        if (fields.size() < 7) continue;

        StageResult result;
        result.tile = fields[0];
        result.stage = fields[1];
        result.status = StatusFromString(fields[2]);
        result.outputPath = fields[3];
        try { result.exitCode = std::stoi(fields[4]); } catch (...) { result.exitCode = 0; }
        result.startedAt = fields[5];
        result.finishedAt = fields[6];

        m_index[Key(result.tile, result.stage)] = m_results.size();
        m_results.push_back(result);
    }
    return true;
}

const StageResult* PipelineState::Find(const std::string& tile, const std::string& stage) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_index.find(Key(tile, stage));
    if (it == m_index.end()) return nullptr;
    return &m_results[it->second];
}

void PipelineState::Record(const StageResult& result) {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::string key = Key(result.tile, result.stage);
    auto it = m_index.find(key);
    if (it != m_index.end()) {
        m_results[it->second] = result;
    } else {
        m_index[key] = m_results.size();
        m_results.push_back(result);
    }
    RewriteManifestFile();
}

std::vector<std::string> PipelineState::TilesWithStatus(StageStatus status) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::string> tiles;
    for (const auto& result : m_results) {
        if (result.status == status &&
            std::find(tiles.begin(), tiles.end(), result.tile) == tiles.end()) {
            tiles.push_back(result.tile);
        }
    }
    return tiles;
}

std::vector<StageResult> PipelineState::ResultsForStage(const std::string& stage) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<StageResult> results;
    for (const auto& result : m_results) {
        if (result.stage == stage) {
            results.push_back(result);
        }
    }
    return results;
}

// Caller already holds m_mutex.
void PipelineState::RewriteManifestFile() const {
    if (m_manifestPath.empty()) return;
    std::filesystem::create_directories(m_manifestPath.parent_path());

    std::ofstream out(m_manifestPath, std::ios::trunc);
    if (!out.is_open()) return;

    out << "tile,stage,status,output_path,exit_code,started_at,finished_at\n";
    for (const auto& result : m_results) {
        out << result.tile << "," << result.stage << "," << StatusToString(result.status) << ","
            << result.outputPath << "," << result.exitCode << ","
            << result.startedAt << "," << result.finishedAt << "\n";
    }
}

} // namespace fusion::batch

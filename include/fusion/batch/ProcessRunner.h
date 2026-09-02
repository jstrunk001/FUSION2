#ifndef FUSION_BATCH_PROCESSRUNNER_H
#define FUSION_BATCH_PROCESSRUNNER_H

#include <string>
#include <vector>
#include <filesystem>

namespace fusion::batch {

struct ProcessResult {
    bool launched{false};
    int exitCode{-1};
};

// Runs exePath with the given arguments as a child process, redirecting its
// combined stdout/stderr to logPath (appended, not overwritten -- a tile's
// log accumulates one section per stage). Blocks until the child exits.
// Windows-only (CreateProcessA), matching the rest of this codebase.
ProcessResult RunProcess(
    const std::filesystem::path& exePath,
    const std::vector<std::string>& args,
    const std::filesystem::path& logPath);

// Directory containing the currently running executable -- used to locate
// sibling tool .exe files (canopymodel.exe, groundfilter.exe, ...) next to
// pipeline.exe by default.
std::filesystem::path GetExecutableDir();

} // namespace fusion::batch

#endif // FUSION_BATCH_PROCESSRUNNER_H

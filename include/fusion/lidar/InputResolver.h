#ifndef FUSION_LIDAR_INPUTRESOLVER_H
#define FUSION_LIDAR_INPUTRESOLVER_H

#include <filesystem>
#include <string>
#include <vector>

namespace fusion::lidar {

// Expands a tool's positional command-line arguments into a flat, sorted
// list of LAS/LAZ file paths. Each token is either a plain file path (kept
// as-is) or a directory (non-recursively scanned for .las/.laz files,
// matched case-insensitively). Multiple tokens -- e.g. several files and/or
// directories given as separate shell arguments -- are all expanded and
// concatenated in argument order.
std::vector<std::filesystem::path> ResolveInputFiles(const std::vector<std::string>& positionalArgs);

} // namespace fusion::lidar

#endif // FUSION_LIDAR_INPUTRESOLVER_H

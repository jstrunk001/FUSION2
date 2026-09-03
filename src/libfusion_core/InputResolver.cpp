#include "fusion/lidar/InputResolver.h"

#include <algorithm>
#include <cctype>

namespace fusion::lidar {

namespace {

bool IsLasOrLazExtension(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return (ext == ".las" || ext == ".laz");
}

// Non-recursive scan of one directory for LAS/LAZ files, sorted for
// deterministic ordering (std::filesystem::directory_iterator makes no
// ordering guarantee).
std::vector<std::filesystem::path> ScanDirectory(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> found;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file() && IsLasOrLazExtension(entry.path())) {
            found.push_back(entry.path());
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

} // namespace

std::vector<std::filesystem::path> ResolveInputFiles(const std::vector<std::string>& positionalArgs) {
    std::vector<std::filesystem::path> resolved;

    for (const auto& arg : positionalArgs) {
        std::filesystem::path token(arg);

        if (std::filesystem::is_directory(token)) {
            auto dirFiles = ScanDirectory(token);
            resolved.insert(resolved.end(), dirFiles.begin(), dirFiles.end());
        } else if (std::filesystem::is_regular_file(token) && IsLasOrLazExtension(token)) {
            resolved.push_back(token);
        }
    }

    return resolved;
}

} // namespace fusion::lidar

#pragma once

#include <fstream>
#include <string>
#include <vector>

// Where the engine's own files live relative to the working directory.
//
// The engine is launched from the repo root, from build/, and from build/<Config>/ (and,
// packaged, from the dist folder next to its assets). Rather than depend on one of those,
// every caller asks here and gets the first candidate that exists. This is NOT the asset
// pipeline's path handling: asset *keys* are AssetPath::normalize's job, and this is only
// "find the file on this disk right now".
namespace EnginePaths {

inline std::string resolve(const std::string& relativePath) {
    const std::vector<std::string> candidates = {
        relativePath,
        "../" + relativePath,
        "../../" + relativePath
    };

    for (const auto& candidate : candidates) {
        std::ifstream file(candidate);
        if (file.good()) {
            return candidate;
        }
    }

    return relativePath;
}

} // namespace EnginePaths

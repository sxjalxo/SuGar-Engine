#include "assets/AssetRegistry.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace {
std::string toLower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(std::tolower(character));
        }
    );
    return value;
}
} // namespace

void AssetRegistry::scan(const std::string& root) {
    assets.clear();

    const std::filesystem::path rootPath = std::filesystem::weakly_canonical(root);
    const std::filesystem::path projectPath = std::filesystem::weakly_canonical(std::filesystem::current_path());
    std::error_code errorCode;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(rootPath, errorCode)) {
        if (errorCode || !entry.is_regular_file()) {
            continue;
        }

        const std::filesystem::path relativePath = std::filesystem::relative(entry.path(), projectPath, errorCode);
        const std::filesystem::path displayPath = errorCode ? entry.path() : relativePath;

        AssetInfo info;
        info.path = displayPath.generic_string();
        info.name = entry.path().filename().string();
        info.extension = toLower(entry.path().extension().string());
        assets.push_back(std::move(info));
    }

    std::sort(
        assets.begin(),
        assets.end(),
        [](const AssetInfo& left, const AssetInfo& right) {
            return left.path < right.path;
        }
    );
}

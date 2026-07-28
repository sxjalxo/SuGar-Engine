#include "assets/AssetManifest.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "assets/AssetHash.h"
#include "assets/AssetPath.h"

namespace {
constexpr const char* HeaderPrefix = "sugar-manifest ";
} // namespace

void AssetManifest::set(const std::string& resourceKey, uint64_t artifactKey) {
    const std::string key = AssetPath::normalize(resourceKey);
    if (key.empty()) {
        return;
    }
    entries[key] = artifactKey;
}

uint64_t AssetManifest::lookup(const std::string& resourceKey) const {
    const std::string key = AssetPath::normalize(resourceKey);
    const auto found = entries.find(key);
    return found == entries.end() ? 0 : found->second;
}

bool AssetManifest::write(const std::string& path, std::string& errorMessage) const {
    std::error_code directoryError;
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, directoryError);
    }

    // Binary so '\n' stays one byte on Windows: a text-mode write would emit CRLF and
    // two platforms would produce different manifest bytes for identical inputs.
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        errorMessage = "could not write manifest: " + path;
        return false;
    }

    file << HeaderPrefix << FormatVersion << " cooker " << AssetHash::CookerVersion << "\n";
    for (const auto& entry : entries) {
        file << entry.first << "\t" << AssetHash::toHex(entry.second) << "\n";
    }

    if (!file) {
        errorMessage = "failed while writing manifest: " + path;
        return false;
    }
    return true;
}

bool AssetManifest::load(const std::string& path, std::string& errorMessage) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        errorMessage = "no manifest at: " + path;
        return false;
    }

    entries.clear();

    std::string header;
    if (!std::getline(file, header)) {
        errorMessage = "empty manifest: " + path;
        return false;
    }
    if (header.rfind(HeaderPrefix, 0) != 0) {
        errorMessage = "not a manifest (bad header): " + path;
        return false;
    }

    // The format version must match. A manifest from another cooker names artifacts this
    // build may not produce the same way, so it is refused rather than half-trusted.
    int formatVersion = 0;
    std::istringstream headerStream(header.substr(std::string(HeaderPrefix).size()));
    headerStream >> formatVersion;
    if (formatVersion != FormatVersion) {
        errorMessage = "manifest is from another format version: " + path;
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) {
            continue;
        }
        const size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            errorMessage = "malformed manifest line (no tab): " + path;
            entries.clear();
            return false;
        }

        const std::string key = line.substr(0, tab);
        const std::string hex = line.substr(tab + 1);
        if (hex.size() != 16) {
            errorMessage = "malformed manifest hash: " + path;
            entries.clear();
            return false;
        }

        uint64_t value = 0;
        for (const char character : hex) {
            value <<= 4;
            if (character >= '0' && character <= '9') {
                value |= static_cast<uint64_t>(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                value |= static_cast<uint64_t>(character - 'a' + 10);
            } else {
                errorMessage = "malformed manifest hash digit: " + path;
                entries.clear();
                return false;
            }
        }
        entries[key] = value;
    }

    return true;
}

bool AssetManifest::existsAt(const std::string& path) {
    std::error_code errorCode;
    return std::filesystem::exists(path, errorCode);
}

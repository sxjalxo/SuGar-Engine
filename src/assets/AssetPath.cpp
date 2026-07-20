#include "assets/AssetPath.h"

#include <vector>

namespace {

// ASCII-only, locale-independent lowering. std::tolower with the ambient locale is a
// determinism hazard the design record calls out by name (tr_TR folds 'I' to a dotless
// 'i'), and cooked output must not depend on the machine's locale.
char lowerAscii(char character) {
    const unsigned char value = static_cast<unsigned char>(character);
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }
    return character;
}

// Splits at the FIRST '#'. First, not last, because a sub-selector may legally contain
// '#' in a glTF clip name while a path may not contain one at all.
void split(const std::string& raw, std::string& path, std::string& sub, bool& hasSub) {
    const size_t hash = raw.find('#');
    hasSub = hash != std::string::npos;
    path = hasSub ? raw.substr(0, hash) : raw;
    sub = hasSub ? raw.substr(hash + 1) : std::string();
}

// Separators, dot segments, and the "assets/" anchor. Returns false when the path
// escapes the project root via an uncollapsible "..".
bool normalizePathPart(const std::string& input, std::string& result) {
    std::string slashed;
    slashed.reserve(input.size());
    for (const char character : input) {
        const char normalized = character == '\\' ? '/' : character;
        if (normalized == '/' && !slashed.empty() && slashed.back() == '/') {
            continue; // collapse runs of '/'
        }
        slashed += lowerAscii(normalized);
    }

    const bool absoluteRoot = !slashed.empty() && slashed.front() == '/';

    std::vector<std::string> segments;
    std::string segment;
    for (size_t i = 0; i <= slashed.size(); i++) {
        if (i == slashed.size() || slashed[i] == '/') {
            if (segment == ".") {
                // drop
            } else if (segment == "..") {
                if (segments.empty()) {
                    return false; // escapes the project root -- not an asset key
                }
                segments.pop_back();
            } else if (!segment.empty()) {
                segments.push_back(segment);
            }
            segment.clear();
            continue;
        }
        segment += slashed[i];
    }

    // Anchor at the FIRST "assets" segment when there is one, so
    // "C:/proj/assets/models/x.obj", "./assets/models/x.obj" and "assets/models/x.obj"
    // are one asset. Without an "assets" segment the path stands as-is, relative to the
    // project root; drive letters and leading '/' are dropped with everything before it.
    size_t start = 0;
    bool anchored = false;
    for (size_t i = 0; i < segments.size(); i++) {
        if (segments[i] == "assets") {
            start = i;
            anchored = true;
            break;
        }
    }

    result.clear();
    for (size_t i = start; i < segments.size(); i++) {
        if (!result.empty()) {
            result += '/';
        }
        result += segments[i];
    }

    // An un-anchored absolute path keeps its leading '/' so it stays absolute rather
    // than silently becoming a project-relative path that resolves somewhere else.
    if (!anchored && absoluteRoot && !result.empty()) {
        result.insert(result.begin(), '/');
    }

    return !result.empty();
}

} // namespace

std::string AssetPath::normalize(const std::string& rawPath) {
    std::string path;
    std::string sub;
    bool hasSub = false;
    split(rawPath, path, sub, hasSub);

    std::string normalizedPath;
    if (!normalizePathPart(path, normalizedPath)) {
        return std::string();
    }

    return join(normalizedPath, hasSub ? sub : std::string());
}

std::string AssetPath::pathOf(const std::string& rawPath) {
    std::string path;
    std::string sub;
    bool hasSub = false;
    split(rawPath, path, sub, hasSub);

    std::string normalizedPath;
    if (!normalizePathPart(path, normalizedPath)) {
        return std::string();
    }
    return normalizedPath;
}

std::string AssetPath::subOf(const std::string& rawPath) {
    std::string path;
    std::string sub;
    bool hasSub = false;
    split(rawPath, path, sub, hasSub);
    return hasSub ? sub : std::string();
}

std::string AssetPath::join(const std::string& normalizedPath, const std::string& sub) {
    if (normalizedPath.empty()) {
        return std::string();
    }
    if (sub.empty()) {
        return normalizedPath;
    }
    return normalizedPath + "#" + sub;
}

bool AssetPath::isSupportedAscii(const std::string& value) {
    for (const char character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte < 0x20 || byte > 0x7E) {
            return false;
        }
    }
    return true;
}

#include "assets/AssetManifest.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "assets/AssetHash.h"
#include "assets/AssetPath.h"

namespace {
constexpr const char* HeaderPrefix = "sugar-manifest ";

// Outcome of reading one line without ever growing the buffer past `maxLength` bytes.
// Ok means a complete line came back -- terminated by '\n', or (std::getline's own
// behavior, preserved here) the final partial line at EOF. Eof means nothing was left
// to read. TooLong means the line ran past maxLength before a '\n' turned up: the
// caller must treat this as a rejection, not as "here is a very long line" -- accepting
// it would mean this function did exactly what it exists to prevent, which is
// materializing an attacker-controlled, unbounded number of bytes into one std::string.
enum class LineResult { Ok, Eof, TooLong };

LineResult readBoundedLine(std::istream& file, size_t maxLength, std::string& outLine) {
    outLine.clear();
    char ch = 0;
    bool sawAny = false;
    while (file.get(ch)) {
        sawAny = true;
        if (ch == '\n') {
            if (!outLine.empty() && outLine.back() == '\r') {
                outLine.pop_back();
            }
            return LineResult::Ok;
        }
        if (outLine.size() >= maxLength) {
            return LineResult::TooLong;
        }
        outLine.push_back(ch);
    }
    return sawAny ? LineResult::Ok : LineResult::Eof;
}
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
    // Same constant load() enforces, deliberately: a cap only one side of the format
    // obeys isn't a cap, it's a trap that springs later on whoever opens the file --
    // possibly a player, on a machine where nobody can fix it. Failing here means a
    // developer hits this at package time instead, where they can still act on it.
    if (entries.size() > MaxEntryCount) {
        errorMessage = "manifest has " + std::to_string(entries.size()) + " entries, exceeds cap (" +
                        std::to_string(MaxEntryCount) + "): " + path;
        return false;
    }

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
    const LineResult headerResult = readBoundedLine(file, MaxLineLength, header);
    if (headerResult == LineResult::Eof) {
        errorMessage = "empty manifest: " + path;
        return false;
    }
    if (headerResult == LineResult::TooLong) {
        // A well-formed header is a couple dozen bytes; a header this long cannot be one
        // this reader ever wrote, so it is refused before it grows any further -- this is
        // also what keeps a newline-free multi-megabyte file from becoming one
        // unbounded std::string.
        errorMessage = "manifest header exceeds line cap: " + path;
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

    size_t entryCount = 0;
    std::string line;
    for (;;) {
        const LineResult lineResult = readBoundedLine(file, MaxLineLength, line);
        if (lineResult == LineResult::Eof) {
            break;
        }
        if (lineResult == LineResult::TooLong) {
            // MaxLineLength is derived from MaxKeyLength (one key + one tab + 16 hex
            // digits), so this is the same guard that catches an oversized key -- and,
            // same as the header case above, what stops a runaway line from being read
            // in full before anyone notices it is too long.
            errorMessage = "manifest line exceeds length cap: " + path;
            entries.clear();
            return false;
        }
        if (line.empty()) {
            continue;
        }

        if (++entryCount > MaxEntryCount) {
            errorMessage = "manifest exceeds entry cap (" + std::to_string(MaxEntryCount) +
                            "): " + path;
            entries.clear();
            return false;
        }

        const size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            errorMessage = "malformed manifest line (no tab): " + path;
            entries.clear();
            return false;
        }

        const std::string key = line.substr(0, tab);
        if (key.empty()) {
            // Not a key any scene can ever reference -- set()/AssetPath::normalize()
            // already refuse to store one, so a manifest containing one could not have
            // come from write(). Reject rather than silently keeping a dead entry.
            errorMessage = "malformed manifest line (empty key): " + path;
            entries.clear();
            return false;
        }

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

        if (entries.find(key) != entries.end()) {
            // Silent last-write-wins would mean a "verified" package's manifest no
            // longer round-trips to its own on-disk bytes -- reject instead of guessing
            // which occurrence the packager meant.
            errorMessage = "duplicate manifest key: " + path;
            entries.clear();
            return false;
        }
        entries[key] = value;
    }

    return true;
}

bool AssetManifest::existsAt(const std::string& path) {
    std::error_code errorCode;
    return std::filesystem::exists(path, errorCode);
}

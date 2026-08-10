#include "core/SaveData.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

namespace {

// std::map, not unordered: save() writes in sorted key order so the file bytes are stable
// across runs -- easy to diff, and no dependence on hash iteration order.
std::map<std::string, std::string> g_store;
std::string g_path = "save.dat";

bool containsNewline(const std::string& value) {
    return value.find('\n') != std::string::npos || value.find('\r') != std::string::npos;
}

} // namespace

namespace SaveData {

void setPath(const std::string& newPath) {
    g_path = newPath;
    g_store.clear();
}

const std::string& path() {
    return g_path;
}

bool load() {
    g_store.clear();

    std::ifstream file(g_path, std::ios::binary);
    if (!file) {
        return true; // no save yet is not an error
    }

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back(); // tolerate CRLF saves
        }
        if (line.empty()) {
            continue;
        }
        const size_t equals = line.find('=');
        if (equals == std::string::npos) {
            g_store.clear();
            return false; // malformed line -- refuse the whole file rather than half-load it
        }
        g_store[line.substr(0, equals)] = line.substr(equals + 1);
    }
    return true;
}

bool save() {
    // Atomic write: fill a temp file, then rename it over the real one. A crash or
    // power loss mid-write can only leave the *temp* half-written; the player's
    // existing save is untouched until the rename swaps in a complete file.
    const std::string tmpPath = g_path + ".tmp";
    {
        std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        for (const auto& [key, value] : g_store) {
            file << key << '=' << value << '\n';
        }
        file.flush();
        if (!file) {
            return false; // write failed — leave the real save intact
        }
    } // stream closed before rename

    std::error_code ec;
    std::filesystem::rename(tmpPath, g_path, ec);
    if (ec) {
        // Some filesystems won't rename onto an existing file; remove then retry.
        std::filesystem::remove(g_path, ec);
        std::filesystem::rename(tmpPath, g_path, ec);
        if (ec) {
            std::filesystem::remove(tmpPath, ec); // don't leave a stray temp behind
            return false;
        }
    }
    return true;
}

bool has(const std::string& key) {
    return g_store.find(key) != g_store.end();
}

std::string getString(const std::string& key, const std::string& fallback) {
    const auto found = g_store.find(key);
    return found == g_store.end() ? fallback : found->second;
}

long long getInt(const std::string& key, long long fallback) {
    const auto found = g_store.find(key);
    if (found == g_store.end()) {
        return fallback;
    }
    try {
        size_t consumed = 0;
        const long long value = std::stoll(found->second, &consumed);
        return consumed == found->second.size() ? value : fallback;
    } catch (...) {
        return fallback; // non-numeric stored value -> the caller's default
    }
}

bool setString(const std::string& key, const std::string& value) {
    if (containsNewline(value) || containsNewline(key) || key.find('=') != std::string::npos) {
        return false; // the line format cannot represent these
    }
    g_store[key] = value;
    return true;
}

void setInt(const std::string& key, long long value) {
    g_store[key] = std::to_string(value);
}

void clear() {
    g_store.clear();
}

} // namespace SaveData

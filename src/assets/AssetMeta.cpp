#include "assets/AssetMeta.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

struct TypeName {
    AssetType type;
    const char* name;
};

// Written to disk -- these spellings are part of the file format.
const TypeName TypeNames[] = {
    { AssetType::Unknown, "unknown" },
    { AssetType::Model,   "model" },
    { AssetType::Texture, "texture" },
    { AssetType::Audio,   "audio" },
    { AssetType::Prefab,  "prefab" },
    { AssetType::Scene,   "scene" },
    { AssetType::UI,      "ui" },
    { AssetType::Font,    "font" },
    { AssetType::NavMesh, "navmesh" },
};

std::string escapeJson(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
            case '\\': escaped += "\\\\"; break;
            case '"':  escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:   escaped += character; break;
        }
    }
    return escaped;
}

void skipSpace(const std::string& text, size_t& position) {
    while (position < text.size() &&
           (text[position] == ' ' || text[position] == '\t' ||
            text[position] == '\n' || text[position] == '\r')) {
        position++;
    }
}

// A flat-object reader, not a general JSON parser: a .meta is one object of scalar
// values by construction, and the engine already owns one JSON parser too many. If
// .meta ever needs nesting, that is the moment to extract SceneSerializer's parser into
// Core rather than to grow this one.
bool parseString(const std::string& text, size_t& position, std::string& out) {
    skipSpace(text, position);
    if (position >= text.size() || text[position] != '"') {
        return false;
    }
    position++;

    out.clear();
    while (position < text.size()) {
        const char character = text[position++];
        if (character == '"') {
            return true;
        }
        if (character != '\\') {
            out += character;
            continue;
        }
        if (position >= text.size()) {
            return false;
        }
        const char escaped = text[position++];
        switch (escaped) {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            default:   return false;
        }
    }
    return false;
}

// Scalar values are read back as their source text: numbers and bools keep their
// spelling, strings lose their quotes. 19A stores settings untyped; each importer
// interprets its own keys in 19C.
bool parseScalar(const std::string& text, size_t& position, std::string& out) {
    skipSpace(text, position);
    if (position >= text.size()) {
        return false;
    }
    if (text[position] == '"') {
        return parseString(text, position, out);
    }

    const size_t start = position;
    while (position < text.size() && text[position] != ',' && text[position] != '}' &&
           text[position] != '\n' && text[position] != '\r') {
        position++;
    }
    out = text.substr(start, position - start);
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
    }
    return !out.empty();
}

} // namespace

const char* assetTypeName(AssetType type) {
    for (const TypeName& entry : TypeNames) {
        if (entry.type == type) {
            return entry.name;
        }
    }
    return "unknown";
}

AssetType assetTypeFromName(const std::string& name) {
    for (const TypeName& entry : TypeNames) {
        if (name == entry.name) {
            return entry.type;
        }
    }
    return AssetType::Unknown;
}

AssetType assetTypeFromExtension(const std::string& lowercaseExtension) {
    if (lowercaseExtension == ".gltf" || lowercaseExtension == ".glb" ||
        lowercaseExtension == ".obj") {
        return AssetType::Model;
    }
    if (lowercaseExtension == ".png" || lowercaseExtension == ".jpg" ||
        lowercaseExtension == ".jpeg" || lowercaseExtension == ".tga" ||
        lowercaseExtension == ".bmp" || lowercaseExtension == ".hdr") {
        return AssetType::Texture;
    }
    if (lowercaseExtension == ".wav" || lowercaseExtension == ".ogg" ||
        lowercaseExtension == ".mp3" || lowercaseExtension == ".flac") {
        return AssetType::Audio;
    }
    if (lowercaseExtension == ".prefab") {
        return AssetType::Prefab;
    }
    if (lowercaseExtension == ".scene" || lowercaseExtension == ".json") {
        return AssetType::Scene;
    }
    if (lowercaseExtension == ".rml" || lowercaseExtension == ".rcss") {
        return AssetType::UI;
    }
    if (lowercaseExtension == ".ttf" || lowercaseExtension == ".otf") {
        return AssetType::Font;
    }
    if (lowercaseExtension == ".navmesh") {
        return AssetType::NavMesh;
    }
    return AssetType::Unknown;
}

std::string AssetMeta::get(const std::string& key, const std::string& fallback) const {
    const auto found = settings.find(key);
    return found == settings.end() ? fallback : found->second;
}

void AssetMeta::set(const std::string& key, const std::string& value) {
    settings[key] = value;
}

float AssetMeta::getFloat(const std::string& key, float fallback) const {
    const auto found = settings.find(key);
    if (found == settings.end()) {
        return fallback;
    }

    // strtof with the C locale, not std::stof: the ambient locale decides whether "1.5"
    // or "1,5" parses, and a cooked artifact must not depend on the machine's locale.
    const char* text = found->second.c_str();
    char* end = nullptr;
    const float value = std::strtof(text, &end);
    return (end == text || end == nullptr) ? fallback : value;
}

bool AssetMeta::getBool(const std::string& key, bool fallback) const {
    const auto found = settings.find(key);
    if (found == settings.end()) {
        return fallback;
    }
    const std::string& value = found->second;
    if (value == "true" || value == "1") {
        return true;
    }
    if (value == "false" || value == "0") {
        return false;
    }
    return fallback;
}

std::string AssetMetaIO::sidecarPath(const std::string& sourcePath) {
    return sourcePath + ".meta";
}

std::string AssetMetaIO::serialize(const AssetMeta& meta) {
    // Fixed field order, map-ordered settings, '\n' endings, no timestamps: the cook
    // key hashes these bytes, so the output must be a function of the content only.
    std::string text;
    text += "{\n";
    text += "  \"version\": " + std::to_string(meta.version) + ",\n";
    text += "  \"type\": \"" + std::string(assetTypeName(meta.type)) + "\",\n";
    text += "  \"settings\": {";

    if (meta.settings.empty()) {
        text += "}\n";
    } else {
        text += "\n";
        size_t index = 0;
        for (const auto& setting : meta.settings) {
            text += "    \"" + escapeJson(setting.first) + "\": \"" +
                    escapeJson(setting.second) + "\"";
            text += (++index < meta.settings.size()) ? ",\n" : "\n";
        }
        text += "  }\n";
    }

    text += "}\n";
    return text;
}

bool AssetMetaIO::read(const std::string& metaPath, AssetMeta& out, std::string& errorMessage) {
    std::ifstream file(metaPath, std::ios::binary);
    if (!file) {
        errorMessage = "no .meta file: " + metaPath;
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    AssetMeta parsed;
    parsed.version = 0;
    parsed.type = out.type; // keep the caller's extension-derived guess unless told otherwise

    size_t position = 0;
    skipSpace(text, position);
    if (position >= text.size() || text[position] != '{') {
        errorMessage = "malformed .meta (expected an object): " + metaPath;
        return false;
    }
    position++;

    while (true) {
        skipSpace(text, position);
        if (position < text.size() && text[position] == '}') {
            position++;
            break;
        }
        if (position >= text.size()) {
            errorMessage = "malformed .meta (unterminated object): " + metaPath;
            return false;
        }

        std::string key;
        if (!parseString(text, position, key)) {
            errorMessage = "malformed .meta (expected a key): " + metaPath;
            return false;
        }

        skipSpace(text, position);
        if (position >= text.size() || text[position] != ':') {
            errorMessage = "malformed .meta (expected ':'): " + metaPath;
            return false;
        }
        position++;

        if (key == "settings") {
            skipSpace(text, position);
            if (position >= text.size() || text[position] != '{') {
                errorMessage = "malformed .meta (settings must be an object): " + metaPath;
                return false;
            }
            position++;

            while (true) {
                skipSpace(text, position);
                if (position < text.size() && text[position] == '}') {
                    position++;
                    break;
                }
                if (position >= text.size()) {
                    errorMessage = "malformed .meta (unterminated settings): " + metaPath;
                    return false;
                }

                std::string settingKey;
                if (!parseString(text, position, settingKey)) {
                    errorMessage = "malformed .meta (expected a setting key): " + metaPath;
                    return false;
                }
                skipSpace(text, position);
                if (position >= text.size() || text[position] != ':') {
                    errorMessage = "malformed .meta (expected ':' in settings): " + metaPath;
                    return false;
                }
                position++;

                std::string settingValue;
                if (!parseScalar(text, position, settingValue)) {
                    errorMessage = "malformed .meta (expected a setting value): " + metaPath;
                    return false;
                }
                parsed.settings[settingKey] = settingValue;

                skipSpace(text, position);
                if (position < text.size() && text[position] == ',') {
                    position++;
                }
            }
        } else {
            std::string value;
            if (!parseScalar(text, position, value)) {
                errorMessage = "malformed .meta (expected a value for '" + key + "'): " + metaPath;
                return false;
            }
            if (key == "version") {
                parsed.version = std::atoi(value.c_str());
            } else if (key == "type") {
                parsed.type = assetTypeFromName(value);
            }
            // Unknown top-level keys are ignored rather than fatal: a newer engine may
            // have written fields this build does not know, and refusing to read the
            // file would lose the developer's settings.
        }

        skipSpace(text, position);
        if (position < text.size() && text[position] == ',') {
            position++;
        }
    }

    if (parsed.version <= 0) {
        errorMessage = "malformed .meta (missing version): " + metaPath;
        return false;
    }

    out = parsed;
    return true;
}

bool AssetMetaIO::write(const std::string& metaPath, const AssetMeta& meta, std::string& errorMessage) {
    const std::string text = serialize(meta);

    // Binary so '\n' stays one byte on Windows: a text-mode write would emit CRLF and
    // the same settings would hash differently per platform.
    std::ofstream file(metaPath, std::ios::binary | std::ios::trunc);
    if (!file) {
        errorMessage = "could not write .meta file: " + metaPath;
        return false;
    }
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) {
        errorMessage = "failed while writing .meta file: " + metaPath;
        return false;
    }
    return true;
}

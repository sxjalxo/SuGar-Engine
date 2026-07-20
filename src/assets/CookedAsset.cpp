#include "assets/CookedAsset.h"

#include <cstring>
#include <filesystem>
#include <fstream>

#include "assets/AssetHash.h"
#include "audio/AudioClip.h"
#include "rendering/Mesh.h"
#include "rendering/Vertex.h"

namespace {

constexpr char Magic[4] = { 'S', 'G', 'C', 'A' };

// --- little-endian scalar writers -------------------------------------------
// Explicit byte order, never a struct dump: the cooked bytes must be a function of the
// content alone, not of the compiler's padding or the machine's endianness.
void putU32(std::vector<uint8_t>& out, uint32_t value) {
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
}

void putU64(std::vector<uint8_t>& out, uint64_t value) {
    for (int i = 0; i < 8; i++) {
        out.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xFF));
    }
}

void putFloat(std::vector<uint8_t>& out, float value) {
    // Bit pattern, not a decimal spelling: text would depend on the C locale and lose
    // the low bits, and a cooked mesh must round-trip a float exactly.
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    putU32(out, bits);
}

struct Reader {
    const std::vector<uint8_t>& bytes;
    size_t position = 0;
    bool ok = true;

    bool has(size_t count) const { return position + count <= bytes.size(); }

    uint32_t u32() {
        if (!has(4)) { ok = false; return 0; }
        const uint32_t value =
            static_cast<uint32_t>(bytes[position]) |
            (static_cast<uint32_t>(bytes[position + 1]) << 8) |
            (static_cast<uint32_t>(bytes[position + 2]) << 16) |
            (static_cast<uint32_t>(bytes[position + 3]) << 24);
        position += 4;
        return value;
    }

    uint64_t u64() {
        if (!has(8)) { ok = false; return 0; }
        uint64_t value = 0;
        for (int i = 0; i < 8; i++) {
            value |= static_cast<uint64_t>(bytes[position + static_cast<size_t>(i)]) << (i * 8);
        }
        position += 8;
        return value;
    }

    float f32() {
        const uint32_t bits = u32();
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    uint8_t u8() {
        if (!has(1)) { ok = false; return 0; }
        return bytes[position++];
    }
};

std::vector<uint8_t> makeHeader(CookedAsset::CookedKind kind, uint64_t payloadBytes) {
    std::vector<uint8_t> header;
    header.insert(header.end(), Magic, Magic + 4);
    putU32(header, CookedAsset::FormatVersion);
    putU32(header, AssetHash::CookerVersion);
    putU32(header, static_cast<uint32_t>(kind));
    putU64(header, payloadBytes);
    return header;
}

bool writeFile(
    const std::string& path,
    CookedAsset::CookedKind kind,
    const std::vector<uint8_t>& payload,
    std::string& errorMessage
) {
    std::error_code directoryError;
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, directoryError);
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        errorMessage = "could not write cooked asset: " + path;
        return false;
    }

    const std::vector<uint8_t> header = makeHeader(kind, payload.size());
    file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    file.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    if (!file) {
        errorMessage = "failed while writing cooked asset: " + path;
        return false;
    }
    return true;
}

bool readFile(
    const std::string& path,
    CookedAsset::CookedKind expectedKind,
    std::vector<uint8_t>& payload,
    std::string& errorMessage
) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        errorMessage = "could not read cooked asset: " + path;
        return false;
    }

    const std::streamoff size = file.tellg();
    file.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!file) {
        errorMessage = "failed while reading cooked asset: " + path;
        return false;
    }

    if (bytes.size() < 24 || std::memcmp(bytes.data(), Magic, 4) != 0) {
        errorMessage = "not a cooked asset: " + path;
        return false;
    }

    Reader reader{ bytes, 4 };
    const uint32_t formatVersion = reader.u32();
    const uint32_t cookerVersion = reader.u32();
    const uint32_t kind = reader.u32();
    const uint64_t payloadBytes = reader.u64();

    // Both counters are checked. A stale artifact is not an error the developer has to
    // act on -- the caller recooks -- but reading it as if it were current would be.
    if (formatVersion != CookedAsset::FormatVersion || cookerVersion != AssetHash::CookerVersion) {
        errorMessage = "cooked asset is from another cooker version: " + path;
        return false;
    }
    if (kind != static_cast<uint32_t>(expectedKind)) {
        errorMessage = "cooked asset has the wrong kind: " + path;
        return false;
    }
    if (!reader.ok || reader.position + payloadBytes != bytes.size()) {
        errorMessage = "cooked asset is truncated: " + path;
        return false;
    }

    payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(reader.position), bytes.end());
    return true;
}

} // namespace

bool CookedAsset::writeMesh(const std::string& path, const Mesh& mesh, std::string& errorMessage) {
    std::vector<uint8_t> payload;
    payload.reserve(mesh.vertices.size() * 52 + mesh.indices.size() * 4 + 8);

    putU32(payload, static_cast<uint32_t>(mesh.vertices.size()));
    putU32(payload, static_cast<uint32_t>(mesh.indices.size()));

    // Field by field, in declaration order. Vertex happens to be padding-free today,
    // but writing it as a struct would make that an invariant nothing enforces.
    for (const Vertex& vertex : mesh.vertices) {
        for (int i = 0; i < 3; i++) { putFloat(payload, vertex.pos[i]); }
        for (int i = 0; i < 3; i++) { putFloat(payload, vertex.normal[i]); }
        for (int i = 0; i < 2; i++) { putFloat(payload, vertex.uv[i]); }
        for (int i = 0; i < 4; i++) { payload.push_back(vertex.joints[i]); }
        for (int i = 0; i < 4; i++) { putFloat(payload, vertex.weights[i]); }
    }
    for (const uint32_t index : mesh.indices) {
        putU32(payload, index);
    }

    return writeFile(path, CookedKind::Mesh, payload, errorMessage);
}

bool CookedAsset::readMesh(const std::string& path, Mesh& out, std::string& errorMessage) {
    std::vector<uint8_t> payload;
    if (!readFile(path, CookedKind::Mesh, payload, errorMessage)) {
        return false;
    }

    Reader reader{ payload, 0 };
    const uint32_t vertexCount = reader.u32();
    const uint32_t indexCount = reader.u32();

    out.vertices.clear();
    out.indices.clear();
    out.vertices.reserve(vertexCount);
    out.indices.reserve(indexCount);

    for (uint32_t i = 0; i < vertexCount && reader.ok; i++) {
        Vertex vertex{};
        for (int component = 0; component < 3; component++) { vertex.pos[component] = reader.f32(); }
        for (int component = 0; component < 3; component++) { vertex.normal[component] = reader.f32(); }
        for (int component = 0; component < 2; component++) { vertex.uv[component] = reader.f32(); }
        for (int component = 0; component < 4; component++) { vertex.joints[component] = reader.u8(); }
        for (int component = 0; component < 4; component++) { vertex.weights[component] = reader.f32(); }
        out.vertices.push_back(vertex);
    }
    for (uint32_t i = 0; i < indexCount && reader.ok; i++) {
        out.indices.push_back(reader.u32());
    }

    if (!reader.ok) {
        errorMessage = "cooked mesh payload is truncated: " + path;
        return false;
    }
    return true;
}

bool CookedAsset::writeTexture(
    const std::string& path,
    const CookedTexture& texture,
    std::string& errorMessage
) {
    const size_t expected = static_cast<size_t>(texture.width) * texture.height * 4;
    if (texture.pixels.size() != expected) {
        errorMessage = "cooked texture pixel count does not match its size: " + path;
        return false;
    }

    std::vector<uint8_t> payload;
    payload.reserve(texture.pixels.size() + 8);
    putU32(payload, texture.width);
    putU32(payload, texture.height);
    payload.insert(payload.end(), texture.pixels.begin(), texture.pixels.end());

    return writeFile(path, CookedKind::Texture, payload, errorMessage);
}

bool CookedAsset::readTexture(const std::string& path, CookedTexture& out, std::string& errorMessage) {
    std::vector<uint8_t> payload;
    if (!readFile(path, CookedKind::Texture, payload, errorMessage)) {
        return false;
    }

    Reader reader{ payload, 0 };
    out.width = reader.u32();
    out.height = reader.u32();
    const size_t expected = static_cast<size_t>(out.width) * out.height * 4;
    if (!reader.ok || payload.size() - reader.position != expected) {
        errorMessage = "cooked texture payload is truncated: " + path;
        return false;
    }

    out.pixels.assign(payload.begin() + static_cast<std::ptrdiff_t>(reader.position), payload.end());
    return true;
}

bool CookedAsset::writeAudio(const std::string& path, const AudioClip& clip, std::string& errorMessage) {
    std::vector<uint8_t> payload;
    payload.reserve(clip.samples.size() * 4 + 16);

    // The mix format is recorded rather than assumed: a cooked clip that outlives a
    // change to AudioMixSampleRate must be detectable, not silently played at the wrong
    // pitch. (Changing the constant also bumps the cooker version, so this is a second
    // line of defence, not the first.)
    putU32(payload, AudioMixChannels);
    putU32(payload, AudioMixSampleRate);
    putU64(payload, clip.frameCount);
    for (const float sample : clip.samples) {
        putFloat(payload, sample);
    }

    return writeFile(path, CookedKind::Audio, payload, errorMessage);
}

bool CookedAsset::readAudio(const std::string& path, AudioClip& out, std::string& errorMessage) {
    std::vector<uint8_t> payload;
    if (!readFile(path, CookedKind::Audio, payload, errorMessage)) {
        return false;
    }

    Reader reader{ payload, 0 };
    const uint32_t channels = reader.u32();
    const uint32_t sampleRate = reader.u32();
    const uint64_t frameCount = reader.u64();

    if (!reader.ok || channels != AudioMixChannels || sampleRate != AudioMixSampleRate) {
        errorMessage = "cooked audio is in another mix format: " + path;
        return false;
    }

    const uint64_t sampleCount = frameCount * channels;
    out.samples.clear();
    out.samples.reserve(static_cast<size_t>(sampleCount));
    for (uint64_t i = 0; i < sampleCount && reader.ok; i++) {
        out.samples.push_back(reader.f32());
    }
    if (!reader.ok) {
        errorMessage = "cooked audio payload is truncated: " + path;
        return false;
    }

    out.frameCount = frameCount;
    return true;
}

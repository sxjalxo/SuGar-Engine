#pragma once

#include <cstdint>
#include <string>
#include <vector>

class Mesh;
class AudioClip;

// The cooked container format (docs/DESIGN_ASSET_PIPELINE.md, Phase 19B).
//
// Runtime = f(cooked): ResourceManager reads these files and nothing else. glTF, OBJ,
// PNG and WAV parsing happens only inside the cooker, so the runtime load path has one
// shape per resource type instead of one per source format.
//
// Every field is written little-endian, byte by byte, rather than by dumping structs:
// a struct dump would bake in this compiler's padding and this machine's endianness,
// and "the cache must recook byte-identically" would quietly become "on this toolchain".
//
// Layout:
//   magic          4  bytes  "SGCA"
//   formatVersion  u32       CookedAsset::FormatVersion
//   cookerVersion  u32       AssetHash::CookerVersion (a mismatch means "recook")
//   kind           u32       CookedKind
//   payloadBytes   u64
//   payload        ...
namespace CookedAsset {

enum class CookedKind : uint32_t {
    Mesh = 1,
    Texture = 2,
    Audio = 3,
};

// Bumped when this container or any payload layout changes. Distinct from
// AssetHash::CookerVersion, which is the *cache invalidation* counter: this one says
// "this file's bytes mean something different now", and bumping it without bumping the
// cache counter would leave readable-but-wrong artifacts on disk -- so in practice they
// move together, and readCooked() checks both.
constexpr uint32_t FormatVersion = 1;

// A decoded image, the cooked form of any source PNG/JPG/TGA/...: tightly packed RGBA8,
// which is exactly what Texture::createFromPixels wants.
struct CookedTexture {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels; // width * height * 4
};

bool writeMesh(const std::string& path, const Mesh& mesh, std::string& errorMessage);
bool readMesh(const std::string& path, Mesh& out, std::string& errorMessage);

bool writeTexture(const std::string& path, const CookedTexture& texture, std::string& errorMessage);
bool readTexture(const std::string& path, CookedTexture& out, std::string& errorMessage);

bool writeAudio(const std::string& path, const AudioClip& clip, std::string& errorMessage);
bool readAudio(const std::string& path, AudioClip& out, std::string& errorMessage);

} // namespace CookedAsset

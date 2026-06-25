#pragma once

#include <string>

class AudioClip;

// Decodes audio files into AudioClip PCM. miniaudio is confined to AudioLoader.cpp
// (it is used here only as a file decoder), so ResourceManager can dispatch audio
// loading without ever seeing a miniaudio type — mirroring how mesh loading goes
// through GltfLoader/ModelLoader.
namespace AudioLoader {

// Decodes `path` into `out` as interleaved f32 at the engine mix format. Returns
// false if the file can't be opened or decoded.
bool loadClip(const std::string& path, AudioClip& out);

} // namespace AudioLoader

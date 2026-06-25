#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// The hand-rolled mixer runs in one fixed format; every clip is decoded to it on
// load so the per-frame mixing loop never converts sample format, channel count,
// or rate at runtime. Shared by AudioClip (the asset), AudioLoader (the decoder),
// and AudioEngine (the device + mixer).
constexpr uint32_t AudioMixChannels = 2;
constexpr uint32_t AudioMixSampleRate = 48000;

// A decoded audio asset: interleaved f32 PCM at the mix format above. Owned and
// cached by ResourceManager exactly like Mesh/Texture (handle + resource key +
// ref counting + hot reload). Unlike those it holds no GPU resource, so there is
// no upload()/destroy() — it is pure CPU data.
class AudioClip {
public:
    std::vector<float> samples; // interleaved, AudioMixChannels per frame
    uint64_t frameCount = 0;

    void setResourceKey(std::string key) { resourceKey = std::move(key); }
    const std::string& getResourceKey() const { return resourceKey; }

private:
    std::string resourceKey;
};

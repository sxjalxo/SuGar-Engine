#include "audio/AudioLoader.h"

#include "audio/AudioClip.h"
#include "miniaudio.h"

#include <vector>

namespace AudioLoader {

bool loadClip(const std::string& path, AudioClip& out) {
    // Ask the decoder to output our exact mix format so the mixer never has to
    // convert sample format / channel count / rate at runtime.
    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, AudioMixChannels, AudioMixSampleRate);
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &config, &decoder) != MA_SUCCESS) {
        return false;
    }

    out.samples.clear();
    out.frameCount = 0;

    constexpr ma_uint64 chunkFrames = 4096;
    std::vector<float> chunk(static_cast<size_t>(chunkFrames) * AudioMixChannels);
    ma_uint64 framesRead = 0;
    while (ma_decoder_read_pcm_frames(&decoder, chunk.data(), chunkFrames, &framesRead) == MA_SUCCESS &&
           framesRead > 0) {
        out.samples.insert(out.samples.end(),
                           chunk.begin(),
                           chunk.begin() + static_cast<size_t>(framesRead * AudioMixChannels));
        out.frameCount += framesRead;
        if (framesRead < chunkFrames) {
            break;
        }
    }

    ma_decoder_uninit(&decoder);
    return out.frameCount > 0;
}

} // namespace AudioLoader

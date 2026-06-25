#include "audio/AudioEngine.h"

#include "audio/AudioClip.h"
#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <mutex>
#include <vector>

namespace {

// The mixer runs entirely in this format. Channels/rate come from AudioClip.h so
// the decoder (AudioLoader) and the device agree; format is always f32.
constexpr ma_format DeviceFormat = ma_format_f32;
constexpr ma_uint32 DeviceChannels = AudioMixChannels;
constexpr ma_uint32 DeviceSampleRate = AudioMixSampleRate;

// One playing instance. `cursor` is a fractional frame index so per-voice pitch
// can resample with linear interpolation.
struct Voice {
    uint32_t id = 0;
    std::shared_ptr<AudioClip> clip;
    double cursor = 0.0;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool active = false;
};

} // namespace

struct AudioEngine::Impl {
    ma_device device{};
    bool deviceStarted = false;

    mutable std::mutex mutex;        // guards voices + paused (shared with audio thread)
    std::vector<Voice> voices;
    bool paused = false;
    uint32_t nextVoiceId = 1;

    // Mixes all active voices into `output`. Runs on the audio thread.
    void mix(float* output, ma_uint32 frameCount) {
        std::memset(output, 0, static_cast<size_t>(frameCount) * DeviceChannels * sizeof(float));

        std::lock_guard<std::mutex> lock(mutex);
        if (paused) {
            return;
        }

        for (Voice& voice : voices) {
            if (!voice.active || !voice.clip) {
                continue;
            }
            const AudioClip& clip = *voice.clip;

            for (ma_uint32 frame = 0; frame < frameCount; frame++) {
                if (voice.cursor >= static_cast<double>(clip.frameCount)) {
                    if (voice.loop && clip.frameCount > 0) {
                        voice.cursor = std::fmod(voice.cursor, static_cast<double>(clip.frameCount));
                    } else {
                        voice.active = false;
                        break;
                    }
                }

                const auto index0 = static_cast<uint64_t>(voice.cursor);
                uint64_t index1 = index0 + 1;
                if (index1 >= clip.frameCount) {
                    index1 = voice.loop ? 0 : index0;
                }
                const float frac = static_cast<float>(voice.cursor - static_cast<double>(index0));

                for (ma_uint32 channel = 0; channel < DeviceChannels; channel++) {
                    const float s0 = clip.samples[index0 * DeviceChannels + channel];
                    const float s1 = clip.samples[index1 * DeviceChannels + channel];
                    output[frame * DeviceChannels + channel] += (s0 + (s1 - s0) * frac) * voice.volume;
                }

                voice.cursor += static_cast<double>(voice.pitch);
            }
        }

        // Hard-clamp the mix so overlapping voices distort gracefully instead of
        // wrapping. (A proper limiter is a later refinement.)
        const ma_uint32 sampleCount = frameCount * DeviceChannels;
        for (ma_uint32 i = 0; i < sampleCount; i++) {
            output[i] = std::clamp(output[i], -1.0f, 1.0f);
        }
    }

    Voice* findVoice(uint32_t id) {
        if (id == 0) {
            return nullptr;
        }
        for (Voice& voice : voices) {
            if (voice.id == id && voice.active) {
                return &voice;
            }
        }
        return nullptr;
    }

    // Device thread entry point. Static so miniaudio can take its address; the
    // Impl* travels through ma_device::pUserData.
    static void dataCallback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frameCount) {
        auto* impl = static_cast<Impl*>(device->pUserData);
        if (impl != nullptr) {
            impl->mix(static_cast<float*>(output), frameCount);
        }
    }
};

AudioEngine::AudioEngine() : impl(std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::init() {
    if (impl->deviceStarted) {
        return true;
    }

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = DeviceFormat;
    config.playback.channels = DeviceChannels;
    config.sampleRate = DeviceSampleRate;
    config.dataCallback = &Impl::dataCallback;
    config.pUserData = impl.get();

    if (ma_device_init(nullptr, &config, &impl->device) != MA_SUCCESS) {
        std::cerr << "[Audio] no playback device; running silently\n";
        return false;
    }
    if (ma_device_start(&impl->device) != MA_SUCCESS) {
        std::cerr << "[Audio] failed to start playback device; running silently\n";
        ma_device_uninit(&impl->device);
        return false;
    }

    impl->deviceStarted = true;
    std::cout << "[Audio] device started (" << DeviceSampleRate << " Hz, "
              << DeviceChannels << " ch)\n";
    return true;
}

void AudioEngine::shutdown() {
    if (impl->deviceStarted) {
        ma_device_uninit(&impl->device); // stops the callback thread first
        impl->deviceStarted = false;
    }
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->voices.clear();
    }
}

void AudioEngine::setPaused(bool paused) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->paused = paused;
}

uint32_t AudioEngine::play(const std::shared_ptr<AudioClip>& clip, float volume, float pitch, bool loop) {
    if (!clip || clip->frameCount == 0) {
        return 0;
    }

    Voice voice;
    voice.clip = clip;
    voice.volume = volume;
    voice.pitch = pitch;
    voice.loop = loop;
    voice.active = true;

    std::lock_guard<std::mutex> lock(impl->mutex);
    // Reclaim finished voices before adding a new one to keep the list bounded.
    impl->voices.erase(
        std::remove_if(impl->voices.begin(), impl->voices.end(),
                       [](const Voice& v) { return !v.active; }),
        impl->voices.end());

    voice.id = impl->nextVoiceId++;
    if (impl->nextVoiceId == 0) {
        impl->nextVoiceId = 1; // 0 is the "invalid" sentinel
    }
    impl->voices.push_back(std::move(voice));
    return impl->voices.back().id;
}

void AudioEngine::setVoiceParams(uint32_t voice, float volume, float pitch) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (Voice* v = impl->findVoice(voice)) {
        v->volume = volume;
        v->pitch = pitch;
    }
}

bool AudioEngine::isActive(uint32_t voice) const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->findVoice(voice) != nullptr;
}

void AudioEngine::stop(uint32_t voice) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (Voice* v = impl->findVoice(voice)) {
        v->active = false;
    }
}

void AudioEngine::stopAll() {
    std::lock_guard<std::mutex> lock(impl->mutex);
    for (Voice& voice : impl->voices) {
        voice.active = false;
    }
    impl->voices.clear();
}

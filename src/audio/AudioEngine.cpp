#include "audio/AudioEngine.h"

#include "audio/AudioClip.h"
#include "miniaudio.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
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

// --- SUGAR_AUDIODBG instrument support (DESIGN_AUDIO_THREAD_OWNERSHIP.md Section 4) ---
// Both histograms (mix duration, lock wait) share one bucket scheme, expressed as a
// fraction of that callback's own deadline (frameCount/sampleRate) rather than an
// absolute time, because frameCount is miniaudio's default and can vary callback to
// callback -- fixed absolute buckets would silently drift meaningless if it ever did.
constexpr int kHistogramBuckets = AudioDebugStats::HistogramBuckets;
constexpr double kBucketUpperEdge[kHistogramBuckets - 1] = {
    0.10, 0.25, 0.50, 0.75, 1.00, 1.50, 2.00, 3.00, 5.00
};
// Representative fraction per bucket (its midpoint), used only to turn the
// histogram into an approximate median -- never exact, and never the verdict.
constexpr double kBucketRepresentative[kHistogramBuckets] = {
    0.05, 0.175, 0.375, 0.625, 0.875, 1.25, 1.75, 2.5, 4.0, 5.0
};

int bucketForFraction(double fraction) {
    for (int i = 0; i < kHistogramBuckets - 1; ++i) {
        if (fraction < kBucketUpperEdge[i]) {
            return i;
        }
    }
    return kHistogramBuckets - 1;
}

using DebugClock = std::chrono::steady_clock;

double nsToMs(uint64_t ns) {
    return static_cast<double>(ns) / 1'000'000.0;
}

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

    // SUGAR_AUDIODBG instrument (DESIGN_AUDIO_THREAD_OWNERSHIP.md Section 4). Read
    // once here, off the audio thread, at construction; mix() branches on this bool
    // and does nothing else -- no chrono call, no atomic touch -- when it's false.
    const bool debugEnabled = std::getenv("SUGAR_AUDIODBG") != nullptr;

    // Below: the ONLY writer is the audio thread inside mix(); the ONLY reader is
    // AudioEngine::debugStats() on the gameplay thread. Relaxed ordering is
    // deliberate -- these are independent diagnostic counters, not a
    // synchronization point, and none of them participate in the mutex's
    // happens-before relationship.
    std::atomic<uint64_t> callbacksObserved{0};
    std::atomic<uint64_t> overruns{0};
    std::atomic<uint64_t> arrivalGaps{0};
    std::atomic<uint64_t> lastDeadlineNs{0};
    std::atomic<int64_t> lastEntryNs{0};   // 0 == sentinel, no previous callback yet
    std::atomic<uint64_t> maxMixDurationNs{0};
    std::atomic<uint64_t> maxLockWaitNs{0};
    std::atomic<uint64_t> maxLockWaitDeadlineNs{0}; // the deadline paired with maxLockWaitNs
    std::atomic<uint64_t> mixDurationHistogram[kHistogramBuckets]{};
    std::atomic<uint64_t> lockWaitHistogram[kHistogramBuckets]{};

    // Called at the very top of mix(), before the lock: records this callback's
    // arrival and, everything after the first callback, the gap since the last one.
    void recordArrival(DebugClock::time_point entry, uint64_t deadlineNs) {
        const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            entry.time_since_epoch()).count();
        const int64_t prevNs = lastEntryNs.exchange(nowNs, std::memory_order_relaxed);
        callbacksObserved.fetch_add(1, std::memory_order_relaxed);
        lastDeadlineNs.store(deadlineNs, std::memory_order_relaxed);
        if (prevNs != 0) { // 0 == no previous callback (this is the first one; nothing to gap)
            const uint64_t gapNs = static_cast<uint64_t>(nowNs - prevNs);
            // "Materially longer" than the period: 50% past the expected deadline.
            if (deadlineNs > 0 && gapNs > deadlineNs + deadlineNs / 2) {
                arrivalGaps.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    // Called immediately after the lock_guard acquires the mutex: this is the
    // lock-wait diagnostic (evidence, never the verdict -- Section 5).
    void recordLockAcquired(DebugClock::time_point entry, uint64_t deadlineNs) {
        const auto waitNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            DebugClock::now() - entry).count());
        if (waitNs > maxLockWaitNs.load(std::memory_order_relaxed)) {
            maxLockWaitNs.store(waitNs, std::memory_order_relaxed);
            maxLockWaitDeadlineNs.store(deadlineNs, std::memory_order_relaxed);
        }
        if (deadlineNs > 0) {
            const int bucket = bucketForFraction(static_cast<double>(waitNs) / static_cast<double>(deadlineNs));
            lockWaitHistogram[bucket].fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Called once mix() is done with the buffer (whether it mixed or returned early
    // because paused): this is the overrun check (Section 5's PROMOTES condition).
    void recordMixDone(DebugClock::time_point entry, uint64_t deadlineNs) {
        const auto durationNs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            DebugClock::now() - entry).count());
        if (durationNs > maxMixDurationNs.load(std::memory_order_relaxed)) {
            maxMixDurationNs.store(durationNs, std::memory_order_relaxed);
        }
        if (deadlineNs > 0) {
            const int bucket = bucketForFraction(static_cast<double>(durationNs) / static_cast<double>(deadlineNs));
            mixDurationHistogram[bucket].fetch_add(1, std::memory_order_relaxed);
            if (durationNs >= deadlineNs) {
                overruns.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    // Mixes all active voices into `output`. Runs on the audio thread.
    void mix(float* output, ma_uint32 frameCount) {
        std::memset(output, 0, static_cast<size_t>(frameCount) * DeviceChannels * sizeof(float));

        DebugClock::time_point entry{};
        uint64_t deadlineNs = 0;
        if (debugEnabled) {
            entry = DebugClock::now();
            deadlineNs = static_cast<uint64_t>(
                (static_cast<double>(frameCount) / static_cast<double>(DeviceSampleRate)) * 1.0e9);
            recordArrival(entry, deadlineNs);
        }

        std::lock_guard<std::mutex> lock(mutex);

        if (debugEnabled) {
            recordLockAcquired(entry, deadlineNs);
        }

        if (paused) {
            if (debugEnabled) {
                recordMixDone(entry, deadlineNs);
            }
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

        if (debugEnabled) {
            recordMixDone(entry, deadlineNs);
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
    // Guard the mixer's fixed-format assumption: it indexes samples as
    // frame*AudioMixChannels+channel, so a clip whose buffer is shorter than that
    // (a mismatched/truncated decode) would read out of bounds on the audio thread.
    // Refuse it here rather than corrupt playback or crash.
    if (clip->samples.size() < clip->frameCount * DeviceChannels) {
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

    // Hard cap: if every voice is still active, steal one rather than let the list
    // grow without bound (a burst of one-shots with no follow-up play() would
    // otherwise never be reclaimed). 64 simultaneous voices is ample for gameplay.
    // Prefer stealing the oldest *non-looping* voice: looping voices are almost
    // always long-lived background music/ambience, and cutting the music because a
    // pile of one-shots fired is the worst-sounding choice. Only when every voice is
    // a loop do we fall back to the oldest overall.
    constexpr size_t MaxVoices = 64;
    if (impl->voices.size() >= MaxVoices) {
        auto victim = std::find_if(impl->voices.begin(), impl->voices.end(),
                                   [](const Voice& v) { return !v.loop; });
        if (victim == impl->voices.end()) {
            victim = impl->voices.begin(); // all looping: steal the oldest
        }
        impl->voices.erase(victim);
    }

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

AudioDebugStats AudioEngine::debugStats() const {
    AudioDebugStats stats;
    stats.callbacksObserved = impl->callbacksObserved.load(std::memory_order_relaxed);
    stats.overruns = impl->overruns.load(std::memory_order_relaxed);
    stats.arrivalGaps = impl->arrivalGaps.load(std::memory_order_relaxed);

    const uint64_t deadlineNs = impl->lastDeadlineNs.load(std::memory_order_relaxed);
    stats.lastDeadlineMs = nsToMs(deadlineNs);

    stats.maxMixDurationMs = nsToMs(impl->maxMixDurationNs.load(std::memory_order_relaxed));

    uint64_t mixCounts[kHistogramBuckets];
    uint64_t mixTotal = 0;
    for (int i = 0; i < kHistogramBuckets; ++i) {
        mixCounts[i] = impl->mixDurationHistogram[i].load(std::memory_order_relaxed);
        stats.mixDurationHistogram[i] = mixCounts[i];
        mixTotal += mixCounts[i];
    }
    if (mixTotal > 0 && deadlineNs > 0) {
        // Approximate median: the bucket whose cumulative count first passes the
        // halfway point, reported via its representative fraction of the (most
        // recent) deadline. Never exact, and per Section 5 never the verdict.
        uint64_t cumulative = 0;
        const uint64_t half = mixTotal / 2;
        for (int i = 0; i < kHistogramBuckets; ++i) {
            cumulative += mixCounts[i];
            if (cumulative > half) {
                stats.medianMixDurationMs = kBucketRepresentative[i] * stats.lastDeadlineMs;
                break;
            }
        }
    }

    const uint64_t maxLockNs = impl->maxLockWaitNs.load(std::memory_order_relaxed);
    const uint64_t maxLockDeadlineNs = impl->maxLockWaitDeadlineNs.load(std::memory_order_relaxed);
    stats.maxLockWaitMs = nsToMs(maxLockNs);
    stats.maxLockWaitFractionOfDeadline =
        maxLockDeadlineNs > 0 ? static_cast<double>(maxLockNs) / static_cast<double>(maxLockDeadlineNs) : 0.0;
    for (int i = 0; i < kHistogramBuckets; ++i) {
        stats.lockWaitHistogram[i] = impl->lockWaitHistogram[i].load(std::memory_order_relaxed);
    }

    return stats;
}

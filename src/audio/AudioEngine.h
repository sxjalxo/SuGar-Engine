#pragma once

#include <cstdint>
#include <memory>

class AudioClip;

// AudioEngine is the low-level audio layer: a thin device backend (miniaudio,
// confined entirely to AudioEngine.cpp) feeding a *hand-rolled* mixer. miniaudio
// is used only to open the playback device; the summing/resampling of voices into
// the output buffer is our own code, in keeping with the engine's hand-rolled
// ethos. Clip *decoding* lives in AudioLoader; clip *ownership/caching* lives in
// ResourceManager. The engine only mixes the decoded AudioClips it is handed.
//
// A "voice" is one playing instance of a clip. play() returns an opaque voice id
// (0 == invalid) that the caller (AudioSystem) uses to keep the voice's params in
// sync with its AudioSourceComponent.
//
// Threading: the device runs a callback on its own thread; the public methods are
// called from the gameplay thread. A short mutex guards the voice list. This is
// the simplest correct design for a first mixer; a lock-free command queue is a
// later optimization (see ROADMAP Phase 9 notes).
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    // Opens the playback device and starts the mixer. Returns false if no audio
    // device is available; the rest of the engine continues to run silently.
    bool init();
    void shutdown();

    // While paused the mixer outputs silence and voice cursors are frozen, so
    // gameplay pause/resume doesn't drop or fast-forward sounds.
    void setPaused(bool paused);

    // Starts a new voice playing `clip`. Returns 0 if the clip is null/empty.
    // The engine holds a shared_ptr to the clip for the voice's lifetime, so the
    // clip stays alive even if ResourceManager releases the source handle.
    uint32_t play(const std::shared_ptr<AudioClip>& clip, float volume, float pitch, bool loop);

    // Live updates for a still-playing voice; no-ops once the voice has ended.
    void setVoiceParams(uint32_t voice, float volume, float pitch);
    bool isActive(uint32_t voice) const;
    void stop(uint32_t voice);

    // Halts every voice (used on Stop / scene replacement).
    void stopAll();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

#include "animation/AnimationClip.h"

TransformSample sampleTrack(const TransformTrack& track, float time) {
    TransformSample sample;
    sample.hasTranslation = sampleChannel(track.translation, time, sample.translation);
    sample.hasRotation = sampleChannel(track.rotation, time, sample.rotation);
    sample.hasScale = sampleChannel(track.scale, time, sample.scale);
    return sample;
}

float computeDuration(const std::vector<TransformTrack>& tracks) {
    float duration = 0.0f;
    for (const TransformTrack& track : tracks) {
        const auto extend = [&duration](const std::vector<float>& times) {
            if (!times.empty()) {
                duration = std::max(duration, times.back());
            }
        };
        extend(track.translation.times);
        extend(track.rotation.times);
        extend(track.scale.times);
    }
    return duration;
}

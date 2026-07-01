#pragma once

#include <cstdint>
#include <deque>
#include <string>

// Storage backend for the time-travel snapshot ring (Phase 11C). The Timeline and
// SuGarApp talk only to this interface, so *how* frames are encoded can evolve
// (full JSON today; binary / delta-keyframe later) without touching the UI or the
// time-travel control flow.
//
//   ISnapshotStorage
//     |- JsonSnapshotStorage    (full scene JSON per frame — the baseline)
//     |- BinarySnapshotStorage  (compact binary — later)
//     |- DeltaSnapshotStorage   (deltas + periodic keyframes — the memory win, later)
class ISnapshotStorage {
public:
    virtual ~ISnapshotStorage() = default;

    virtual void clear() = 0;

    // Appends the newest frame (a serialized scene). Backends may evict the oldest
    // once capacity is exceeded.
    virtual void push(const std::string& sceneState) = 0;

    // Number of frames currently retained.
    virtual int count() const = 0;

    // Reconstructs the scene state at [0, count) — index 0 is the oldest retained
    // frame, count-1 the newest.
    virtual std::string get(int index) const = 0;

    // Monotonically increasing global number of the frame at `index`. Unlike the
    // index, it is stable across eviction-driven shifts, so timeline bookmarks can
    // key off it. frameNumber(0) rises as old frames are evicted.
    virtual uint64_t frameNumber(int index) const = 0;
};

// Baseline backend: keeps the last `capacity` frames as their full serialized
// strings. Correct and simple; memory is capacity x scene size (fine for M2, the
// reason delta/binary backends exist on the roadmap).
class JsonSnapshotStorage : public ISnapshotStorage {
public:
    explicit JsonSnapshotStorage(size_t capacity) : capacity(capacity == 0 ? 1 : capacity) {}

    void clear() override {
        frames.clear();
        firstFrameNumber = 0;
    }

    void push(const std::string& sceneState) override {
        frames.push_back(sceneState);
        while (frames.size() > capacity) {
            frames.pop_front();
            ++firstFrameNumber;
        }
    }

    int count() const override { return static_cast<int>(frames.size()); }

    std::string get(int index) const override {
        if (index < 0 || index >= static_cast<int>(frames.size())) {
            return {};
        }
        return frames[static_cast<size_t>(index)];
    }

    uint64_t frameNumber(int index) const override {
        return firstFrameNumber + static_cast<uint64_t>(index < 0 ? 0 : index);
    }

private:
    std::deque<std::string> frames;
    size_t capacity;
    uint64_t firstFrameNumber = 0; // global number of frames[0]
};

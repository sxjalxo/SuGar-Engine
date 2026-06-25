#pragma once

#include <cstdint>

// Runtime mode of the engine. The editor authors the scene in Edit; pressing
// Play snapshots the scene and begins ticking gameplay systems; Stop restores
// the snapshot and returns to Edit.
enum class EngineState : uint8_t {
    Edit,
    Play,
    Paused
};

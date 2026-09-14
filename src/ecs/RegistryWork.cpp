#include "ecs/Registry.h"

// DESIGN_RENDER_CPU_TIMING.md Section 12 -- the counters live HERE, in one translation unit
// inside SuGarCore, and not in an inline accessor in Registry.h.
//
// The first version used a function-local static inside an inline function in the
// header. It compiled, it ran, and every counter reported ZERO, because
// `Skinning::computeJointMatrices` is built into SuGarCore (CMakeLists.txt) while
// `buildDrawListFromECS` is built into the SuGarEngine executable: two modules, two
// copies of the static, so the delta taken around the call could never be anything but
// zero. Section 11.2 had written the DLL-copy hazard down as a GAME-DLL concern and did not
// recognise it as an engine-to-Core one.
//
// A non-inline definition in a Core .cpp is exported (SuGarCore builds with
// WINDOWS_EXPORT_ALL_SYMBOLS), so every module shares one instance.
RegistryWorkCounters& registryWorkCounters() {
    static RegistryWorkCounters counters;
    return counters;
}

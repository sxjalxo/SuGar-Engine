#pragma once

class Registry;

// Runs every entity's named Behavior for one gameplay step. Extracted from SuGarApp
// into Core so it is headless-testable (behaviors, BehaviorRegistry and Registry are
// all Core; the engine loop only supplies the cadence).
namespace ScriptSystem {

// Ticks all ScriptComponents once. Contract that the tests pin:
//  - Deterministic order: entities tick in ascending id order, stable run-to-run and
//    after a snapshot restore (unordered_map order would not be).
//  - Safe under structural change: a behavior may createEntity / destroyEntity during
//    its tick (spawn a bullet, kill a target). Iteration is over a private id
//    snapshot, so no iterator is invalidated; an entity destroyed earlier this step
//    is skipped, and an entity spawned this step first ticks on the NEXT call.
void run(Registry& registry, float deltaTime);

} // namespace ScriptSystem

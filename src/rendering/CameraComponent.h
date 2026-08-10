#pragma once

// Marks an entity as a game camera (the Unity/Godot/Unreal "Camera is a component"
// model). Lives in Core so a game module — which links only Core and cannot touch
// the engine's render Camera — can define the view by placing this on an entity and
// driving that entity's Transform (WASD/mouse-look in a behavior, a follow rig, etc).
//
// Authoritative fields ONLY. The camera *pose* (eye position + look direction) is
// NOT stored here: it is a function of the entity's world transform, so duplicating
// it would create a second owner that could disagree after a snapshot restore
// (RULES.md Rule 21b — if the present can recompute it, it is not state). The engine
// renderer reads the active camera's world transform each frame: position = the
// transform's world translation, forward = rotation * (0,0,-1), up = rotation *
// (0,1,0) — the glTF/OpenGL "-Z forward" convention the projection already assumes.
//
// Absent from a scene ⇒ no game camera, and the engine keeps its editor/orbit
// camera (fully back-compatible with every pre-existing scene).
struct CameraComponent {
    float fovDegrees = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    // When several cameras exist, the engine uses the lowest-entity-id *active* one
    // (deterministic, matching the id-order discipline used across the engine). A
    // game switches views by toggling `active`, not by reordering entities.
    bool active = true;
};

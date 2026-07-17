#pragma once

#include <map>
#include <string>

// Phase 17A — the authoritative half of animation. RULES.md Rule 21 names this
// exact component as its worked example of what *not* to do:
//
//     Animator { float currentTime; }   // hidden authoritative state
//             ↓ snapshot → restore
//     animation jumps
//
// So playback time lives here, in ECS, where it snapshots and time-travels like
// any other component — and the pose is recomputed from it every fixed step
// rather than stored. See docs/DESIGN_ANIMATION.md.
struct AnimationPlayerComponent {
    // Clip name, resolved through AnimationClipRegistry. A name (not a pointer or
    // an index) because it serializes, survives a snapshot as a plain string, and
    // lets a hot-reloaded clip table be swapped underneath a running animation
    // without anything dangling. Same reasoning as ScriptComponent::behavior.
    std::string clip;

    // Seconds into the clip. Authoritative: scrub to frame N twice and the pose
    // must be identical, which is only true if this is restored rather than
    // re-derived.
    float time = 0.0f;

    // Playback rate multiplier. Gameplay mutates it (haste, slow-motion, rewind
    // via a negative value), so it is game state, not a tuning constant.
    float speed = 1.0f;

    bool playing = true;

    // Loop vs. one-shot. Authoritative rather than a clip property because the
    // same clip is legitimately looped in one place and fired once in another.
    // A one-shot that reaches the end clears `playing`, which is how gameplay
    // reads "the clip finished" without asking the animator for a derived pose.
    bool loop = true;
};

// Phase 17C — marks a mesh as skinned by a named skin. Deliberately a **reference,
// not state**: it names bind data in SkinRegistry the same way MeshComponent names
// geometry, and that is all it does.
//
// Note what is *not* here: no joint entity list, no bone transforms, no joint
// matrices, no bind pose. The joints are ordinary entities that the animation
// system already poses, and the joint matrices are derived from them each frame
// (Skinning.h). Caching any of it here would create a second source of truth that
// a snapshot restore could leave disagreeing with the transforms — RULES.md
// Rule 21. See docs/DESIGN_ANIMATION.md.
struct SkinnedMeshComponent {
    std::string skin; // key into SkinRegistry ("<path>#<skinName>")
};

// Phase 17D — a character driven by a state machine / blend tree instead of one
// fixed clip. Use this *or* AnimationPlayerComponent on an entity, never both: they
// both write the subtree's transforms, and two writers is two owners.
//
// Everything here is authoritative. The test is the usual one: replay the same
// inputs twice and nothing may disagree — so anything a later step reads must be
// restored, not re-guessed.
struct AnimationStateComponent {
    std::string graph; // key into AnimationGraphRegistry

    // The active state, and how far through it we are. Both authoritative for the
    // same reason `time` is in AnimationPlayerComponent (RULES.md Rule 21).
    //
    // **Phase, not seconds** — normalized [0,1) across the state. A blend tree mixes
    // clips of different lengths (a walk cycle is slower than a run), and blending
    // them by wall-clock time slides the feet; blending by phase keeps the contacts
    // aligned. Seconds would also make the time base shift as the blend parameter
    // moved, since the state's effective duration changes with it. Named `phase` so
    // it can't be mistaken for AnimationPlayerComponent::time, which *is* seconds.
    std::string currentState;
    float statePhase = 0.0f;

    // Mid-transition, when non-empty: the state being blended *towards*. The pose is
    // then a blend of currentState @ statePhase and transitionTarget @ targetPhase.
    std::string transitionTarget;
    float targetPhase = 0.0f;

    // How far the cross-fade has run, and how long it lasts.
    //
    // This is the design record's sharpest call: **transition progress is
    // authoritative**, even though the identical-looking UI tween is derived. A UI
    // tween can snap to target because only the eye reads it; a transition mid-blend
    // *is* the character's actual pose, and therefore root motion, hitboxes, and
    // what the player sees at frame N. Scrub to frame N twice and it must match.
    float transitionElapsed = 0.0f;
    float transitionDuration = 0.0f;

    bool transitioning() const { return !transitionTarget.empty(); }
};

// Parameters the graph's transitions and blend trees read (speed, health, "attack").
//
// This is **gameplay's state**, which the animator only ever reads — gameplay
// behaviors write it. It lives in its own component rather than inside the animator
// so there is exactly one copy: an animator caching a parameter is a second source
// of truth that a snapshot restore can leave disagreeing with gameplay, which is a
// Rule 21 bug wearing a performance costume.
//
// std::map, not unordered_map: iteration order is part of the serialized output, and
// a scene file that reorders itself between saves is a diff nobody can review.
struct AnimationParametersComponent {
    std::map<std::string, float> values;

    float get(const std::string& name, float fallback = 0.0f) const {
        const auto it = values.find(name);
        return it == values.end() ? fallback : it->second;
    }
};

#pragma once

#include <string>
#include <vector>

// Phase 17D — blend trees + state machines, as **immutable asset data**. Same
// classification as AnimationClip and Skin: the graph's shape never changes at
// runtime, so none of this is snapshotted. What *does* change — which state is
// active, how far a transition has run — is authoritative and lives in ECS
// (AnimationStateComponent). See docs/DESIGN_ANIMATION.md.
//
// Everything here refers to clips and states **by name**, for the same reason
// components do: names survive serialization and a re-imported graph, indices don't.

// One input of a 1D blend tree: a clip that is fully weighted when the tree's
// parameter equals `threshold`, fading to nothing at its neighbours.
struct BlendTreeEntry {
    std::string clip;
    float threshold = 0.0f;
};

// A state plays either one clip, or a 1D blend tree driven by a parameter (the
// canonical example: idle/walk/run blended by speed). 1D only — 2D directional
// blending is a real thing and deliberately not guessed at until an actual character
// needs it (the CUBICSPLINE lesson from 17B).
struct AnimationGraphState {
    std::string name;

    // Single-clip state. Empty when this state uses a blend tree.
    std::string clip;

    // Blend tree. `blendParameter` names a value in AnimationParametersComponent;
    // entries must be sorted by threshold (the loader/author's job — evaluation
    // assumes it, and unsorted entries would blend the wrong neighbours).
    std::string blendParameter;
    std::vector<BlendTreeEntry> blendEntries;

    float speed = 1.0f;
    bool loop = true;

    bool isBlendTree() const { return !blendEntries.empty(); }
};

// How a parameter is compared to decide a transition.
enum class TransitionCondition {
    Greater,
    Less,
    // Fires when the source state's clip reaches its end. The only condition that
    // reads playback rather than a parameter — it exists because "attack finished ->
    // idle" is otherwise impossible to express without gameplay polling a derived
    // pose, which is exactly the coupling Rule 21 forbids.
    OnFinished
};

struct AnimationTransition {
    std::string from;      // source state; empty means "from any state"
    std::string to;
    std::string parameter; // unused for OnFinished
    TransitionCondition condition = TransitionCondition::Greater;
    float threshold = 0.0f;
    float duration = 0.2f; // seconds to cross-fade; 0 snaps
};

struct AnimationGraph {
    std::string name;
    std::string entryState;
    std::vector<AnimationGraphState> states;
    std::vector<AnimationTransition> transitions;

    const AnimationGraphState* findState(const std::string& stateName) const;
};

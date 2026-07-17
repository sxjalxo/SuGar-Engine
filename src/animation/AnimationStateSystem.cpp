#include "animation/AnimationStateSystem.h"

#include "animation/AnimationClip.h"
#include "animation/AnimationClipRegistry.h"
#include "animation/AnimationComponents.h"
#include "animation/AnimationGraph.h"
#include "animation/AnimationGraphRegistry.h"
#include "animation/Pose.h"
#include "ecs/Registry.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace {

float wrapPhase(float phase) {
    float wrapped = std::fmod(phase, 1.0f);
    if (wrapped < 0.0f) {
        wrapped += 1.0f;
    }
    return wrapped;
}

// Which two blend-tree entries surround `value`, and how far between them it sits.
// Entries are assumed sorted by threshold. Outside the range, the end entry wins
// outright rather than extrapolating — a speed above the fastest clip should play
// the fastest clip, not an exaggerated one.
struct BlendPick {
    const BlendTreeEntry* a = nullptr;
    const BlendTreeEntry* b = nullptr;
    float weight = 0.0f; // 0 = all a, 1 = all b
};

BlendPick pickBlend(const std::vector<BlendTreeEntry>& entries, float value) {
    BlendPick pick;
    if (entries.empty()) {
        return pick;
    }
    if (entries.size() == 1 || value <= entries.front().threshold) {
        pick.a = pick.b = &entries.front();
        return pick;
    }
    if (value >= entries.back().threshold) {
        pick.a = pick.b = &entries.back();
        return pick;
    }
    for (size_t i = 0; i + 1 < entries.size(); i++) {
        const BlendTreeEntry& low = entries[i];
        const BlendTreeEntry& high = entries[i + 1];
        if (value >= low.threshold && value <= high.threshold) {
            pick.a = &low;
            pick.b = &high;
            const float span = high.threshold - low.threshold;
            // Coincident thresholds have no "between"; dividing by the zero span
            // would put a NaN into every bone.
            pick.weight = span > 0.0f ? (value - low.threshold) / span : 0.0f;
            return pick;
        }
    }
    pick.a = pick.b = &entries.back();
    return pick;
}

// A state's duration in seconds: the clip's, or the blend of the two active clips'.
// Needed because phase is normalized — converting phase back to seconds (to advance
// it) requires knowing how long the state actually lasts right now.
float stateDuration(const AnimationGraphState& state, float parameter) {
    if (!state.isBlendTree()) {
        const AnimationClip* clip = AnimationClipRegistry::get(state.clip);
        return clip == nullptr ? 0.0f : clip->duration;
    }

    const BlendPick pick = pickBlend(state.blendEntries, parameter);
    if (pick.a == nullptr) {
        return 0.0f;
    }
    const AnimationClip* clipA = AnimationClipRegistry::get(pick.a->clip);
    const AnimationClip* clipB = AnimationClipRegistry::get(pick.b->clip);
    const float durationA = clipA == nullptr ? 0.0f : clipA->duration;
    const float durationB = clipB == nullptr ? 0.0f : clipB->duration;
    return durationA + (durationB - durationA) * pick.weight;
}

// Samples a state at `phase` into `out`. Each clip is sampled at its *own*
// phase*duration, which is what keeps a walk and a run in step while blending.
bool evaluateState(const AnimationGraphState& state, float phase, float parameter, Pose& out, Pose& scratch) {
    if (!state.isBlendTree()) {
        const AnimationClip* clip = AnimationClipRegistry::get(state.clip);
        if (clip == nullptr) {
            return false;
        }
        samplePose(*clip, phase * clip->duration, out);
        return true;
    }

    const BlendPick pick = pickBlend(state.blendEntries, parameter);
    if (pick.a == nullptr) {
        return false;
    }
    const AnimationClip* clipA = AnimationClipRegistry::get(pick.a->clip);
    const AnimationClip* clipB = AnimationClipRegistry::get(pick.b->clip);
    if (clipA == nullptr && clipB == nullptr) {
        return false;
    }
    if (clipA == nullptr || clipB == nullptr || pick.a == pick.b) {
        const AnimationClip* only = clipA != nullptr ? clipA : clipB;
        samplePose(*only, phase * only->duration, out);
        return true;
    }

    samplePose(*clipA, phase * clipA->duration, out);
    samplePose(*clipB, phase * clipB->duration, scratch);
    Pose blended;
    blendPoses(out, scratch, pick.weight, blended);
    out = std::move(blended);
    return true;
}

bool conditionHolds(const AnimationTransition& transition,
                    const AnimationGraphState& from,
                    float phase,
                    const AnimationParametersComponent* parameters) {
    switch (transition.condition) {
        case TransitionCondition::OnFinished:
            // Only a one-shot can finish; a looping state never does, and saying so
            // explicitly beats a transition that silently never fires.
            return !from.loop && phase >= 1.0f;
        case TransitionCondition::Greater:
            return parameters != nullptr && parameters->get(transition.parameter) > transition.threshold;
        case TransitionCondition::Less:
            return parameters != nullptr && parameters->get(transition.parameter) < transition.threshold;
    }
    return false;
}

} // namespace

namespace AnimationStateSystem {

void update(Registry& registry, float dt) {
    const Registry& readOnly = registry;

    // Scratch buffers, reused across entities. Derived — not state.
    Pose currentPose;
    Pose targetPose;
    Pose scratch;
    Pose finalPose;

    for (auto& [entity, machine] : registry.animationStates.getAll()) {
        const AnimationGraph* graph = AnimationGraphRegistry::get(machine.graph);
        if (graph == nullptr) {
            continue; // graph not registered (yet): inert rather than fatal
        }

        // Entering the graph for the first time, or after a state was renamed away.
        if (machine.currentState.empty() || graph->findState(machine.currentState) == nullptr) {
            machine.currentState = graph->entryState;
            machine.statePhase = 0.0f;
            machine.transitionTarget.clear();
            machine.transitionElapsed = 0.0f;
            machine.transitionDuration = 0.0f;
        }

        const AnimationGraphState* current = graph->findState(machine.currentState);
        if (current == nullptr) {
            continue;
        }

        const AnimationParametersComponent* parameters =
            readOnly.animationParameters.has(entity) ? &readOnly.animationParameters.get(entity) : nullptr;
        const float blendValue = (parameters != nullptr && !current->blendParameter.empty())
            ? parameters->get(current->blendParameter)
            : 0.0f;

        // --- advance the active state -------------------------------------------
        const float duration = stateDuration(*current, blendValue);
        if (duration > 0.0f) {
            machine.statePhase += (dt * current->speed) / duration;
            machine.statePhase = current->loop
                ? wrapPhase(machine.statePhase)
                : std::min(std::max(machine.statePhase, 0.0f), 1.0f);
        }

        // --- advance or complete a transition ------------------------------------
        const AnimationGraphState* target = machine.transitioning()
            ? graph->findState(machine.transitionTarget)
            : nullptr;

        if (machine.transitioning() && target == nullptr) {
            machine.transitionTarget.clear(); // target state vanished from the graph
        } else if (target != nullptr) {
            const float targetBlend = (parameters != nullptr && !target->blendParameter.empty())
                ? parameters->get(target->blendParameter)
                : 0.0f;
            const float targetDuration = stateDuration(*target, targetBlend);
            if (targetDuration > 0.0f) {
                machine.targetPhase += (dt * target->speed) / targetDuration;
                machine.targetPhase = target->loop
                    ? wrapPhase(machine.targetPhase)
                    : std::min(std::max(machine.targetPhase, 0.0f), 1.0f);
            }

            machine.transitionElapsed += dt;
            if (machine.transitionElapsed >= machine.transitionDuration) {
                // Arrived: the target becomes the state, carrying its phase across so
                // the clip doesn't jump back to the start on completion.
                machine.currentState = machine.transitionTarget;
                machine.statePhase = machine.targetPhase;
                machine.transitionTarget.clear();
                machine.targetPhase = 0.0f;
                machine.transitionElapsed = 0.0f;
                machine.transitionDuration = 0.0f;
                target = nullptr;
                current = graph->findState(machine.currentState);
                if (current == nullptr) {
                    continue;
                }
            }
        }

        // --- pick a transition ---------------------------------------------------
        // Only when not already mid-blend: interrupting a cross-fade would need a
        // second outgoing pose to blend from, and "queue vs. interrupt" is a real
        // design question that no real character has asked of this engine yet.
        if (!machine.transitioning()) {
            for (const AnimationTransition& transition : graph->transitions) {
                // Empty `from` means "from any state" — the usual home for a death or
                // hit reaction.
                if (!transition.from.empty() && transition.from != machine.currentState) {
                    continue;
                }
                if (transition.to == machine.currentState) {
                    continue;
                }
                if (graph->findState(transition.to) == nullptr) {
                    continue;
                }
                if (!conditionHolds(transition, *current, machine.statePhase, parameters)) {
                    continue;
                }

                // First match wins, in declared order — deterministic, and the author
                // controls priority by ordering.
                if (transition.duration <= 0.0f) {
                    machine.currentState = transition.to;
                    machine.statePhase = 0.0f;
                    current = graph->findState(machine.currentState);
                } else {
                    machine.transitionTarget = transition.to;
                    machine.targetPhase = 0.0f;
                    machine.transitionElapsed = 0.0f;
                    machine.transitionDuration = transition.duration;
                }
                break;
            }
        }

        if (current == nullptr) {
            continue;
        }

        // --- evaluate + apply ----------------------------------------------------
        const float currentBlend = (parameters != nullptr && !current->blendParameter.empty())
            ? parameters->get(current->blendParameter)
            : 0.0f;
        if (!evaluateState(*current, machine.statePhase, currentBlend, currentPose, scratch)) {
            continue;
        }

        const AnimationGraphState* blendTarget = machine.transitioning()
            ? graph->findState(machine.transitionTarget)
            : nullptr;

        if (blendTarget != nullptr) {
            const float targetBlend = (parameters != nullptr && !blendTarget->blendParameter.empty())
                ? parameters->get(blendTarget->blendParameter)
                : 0.0f;
            if (evaluateState(*blendTarget, machine.targetPhase, targetBlend, targetPose, scratch)) {
                const float weight = machine.transitionDuration > 0.0f
                    ? machine.transitionElapsed / machine.transitionDuration
                    : 1.0f;
                blendPoses(currentPose, targetPose, weight, finalPose);
                applyPose(registry, entity, finalPose);
                continue;
            }
        }

        applyPose(registry, entity, currentPose);
    }
}

} // namespace AnimationStateSystem

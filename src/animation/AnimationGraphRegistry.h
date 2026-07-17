#pragma once

#include <string>
#include <unordered_map>

#include "animation/AnimationGraph.h"

// Maps graph names -> immutable graph data. The third instance of the same pattern
// (BehaviorRegistry -> AnimationClipRegistry -> SkinRegistry -> this), and
// deliberately so: AnimationStateComponent stores only the name, so a snapshot holds
// a string, a re-authored graph can be swapped underneath a running character, and
// nothing dangles.
class AnimationGraphRegistry {
public:
    // Registering an existing name replaces the graph. Pointers previously returned
    // by get() are invalidated, so callers resolve by name per step.
    static void registerGraph(const std::string& name, AnimationGraph graph);

    static const AnimationGraph* get(const std::string& name);

    static bool has(const std::string& name);

    static void clear();

private:
    // In a .cpp so the exe, Core and the game DLL share one table.
    static std::unordered_map<std::string, AnimationGraph>& table();
};

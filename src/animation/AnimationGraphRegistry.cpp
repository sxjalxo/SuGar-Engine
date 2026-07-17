#include "animation/AnimationGraphRegistry.h"

#include <utility>

const AnimationGraphState* AnimationGraph::findState(const std::string& stateName) const {
    for (const AnimationGraphState& state : states) {
        if (state.name == stateName) {
            return &state;
        }
    }
    return nullptr;
}

std::unordered_map<std::string, AnimationGraph>& AnimationGraphRegistry::table() {
    static std::unordered_map<std::string, AnimationGraph> instance;
    return instance;
}

void AnimationGraphRegistry::registerGraph(const std::string& name, AnimationGraph graph) {
    table()[name] = std::move(graph);
}

const AnimationGraph* AnimationGraphRegistry::get(const std::string& name) {
    const auto it = table().find(name);
    return it == table().end() ? nullptr : &it->second;
}

bool AnimationGraphRegistry::has(const std::string& name) {
    return table().find(name) != table().end();
}

void AnimationGraphRegistry::clear() {
    table().clear();
}

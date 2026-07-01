#pragma once

// A tiny query language over the ECS for the editor's query console (Phase 11B).
// Grammar:  <component> [where <field> <op> <value>]
//   e.g.  rigidbody where vel.y < 0
//         transform where pos.y > 5
//         audiosource
// Operators: < <= > >= == != . This is engine logic (a query layer over the
// authoritative ECS), so it lives here and is unit-testable without Vulkan.

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "ecs/Registry.h"

namespace EntityQuery {

struct Result {
    std::vector<Entity> entities;
    std::string error; // empty when the query parsed and ran
};

inline std::string toLower(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return value;
}

// Canonical component name (accepts a few friendly aliases).
inline std::string normalizeComponent(const std::string& name) {
    if (name == "rigid" || name == "body") return "rigidbody";
    if (name == "audio") return "audiosource";
    if (name == "listener") return "audiolistener";
    return name;
}

// Fills `out` with every entity that has the named component. Returns false for
// an unknown component name.
inline bool collectEntitiesWith(const Registry& registry, const std::string& component, std::vector<Entity>& out) {
    auto gather = [&out](const auto& storage) {
        for (const auto& entry : storage.getAll()) {
            out.push_back(entry.first);
        }
    };
    if (component == "transform")          { gather(registry.transforms); }
    else if (component == "mesh")          { gather(registry.meshes); }
    else if (component == "material")      { gather(registry.materials); }
    else if (component == "rigidbody")     { gather(registry.rigidBodies); }
    else if (component == "collider")      { gather(registry.colliders); }
    else if (component == "script")        { gather(registry.scripts); }
    else if (component == "audiosource")   { gather(registry.audioSources); }
    else if (component == "audiolistener") { gather(registry.audioListeners); }
    else if (component == "prefab")        { gather(registry.prefabInstances); }
    else if (component == "name")          { gather(registry.names); }
    else {
        return false;
    }
    std::sort(out.begin(), out.end());
    return true;
}

// Numeric value of `component.field` for `entity`, or nullopt if the field isn't
// a known numeric field of that component. Fields may be scalar (mass) or a
// vector axis (vel.y, pos.x, scale.z).
inline std::optional<float> fieldValue(const Registry& registry, Entity entity,
                                       const std::string& component, const std::string& field) {
    std::string base = field;
    std::string axis;
    const size_t dot = field.find('.');
    if (dot != std::string::npos) {
        base = field.substr(0, dot);
        axis = field.substr(dot + 1);
    }
    auto pickAxis = [&axis](const glm::vec3& v) -> std::optional<float> {
        if (axis == "x") return v.x;
        if (axis == "y") return v.y;
        if (axis == "z") return v.z;
        return std::nullopt;
    };

    if (component == "transform" && registry.transforms.has(entity)) {
        const Transform& t = registry.transforms.get(entity).transform;
        if (base == "pos" || base == "position") return pickAxis(t.position);
        if (base == "scale") return pickAxis(t.scale);
    } else if (component == "rigidbody" && registry.rigidBodies.has(entity)) {
        const RigidBodyComponent& b = registry.rigidBodies.get(entity);
        if (base == "vel" || base == "velocity") return pickAxis(b.velocity);
        if (axis.empty() && base == "mass") return b.mass;
        if (axis.empty() && base == "restitution") return b.restitution;
        if (axis.empty() && base == "friction") return b.friction;
    } else if (component == "collider" && registry.colliders.has(entity)) {
        const ColliderComponent& c = registry.colliders.get(entity);
        if (axis.empty() && base == "radius") return c.radius;
        if (base == "halfextents" || base == "extents") return pickAxis(c.halfExtents);
    } else if (component == "material" && registry.materials.has(entity)) {
        const Material& m = registry.materials.get(entity).material;
        if (axis.empty() && base == "metallic") return m.metallic;
        if (axis.empty() && base == "roughness") return m.roughness;
        if (axis.empty() && base == "ao") return m.ao;
    } else if (component == "audiosource" && registry.audioSources.has(entity)) {
        const AudioSourceComponent& s = registry.audioSources.get(entity);
        if (axis.empty() && base == "volume") return s.volume;
        if (axis.empty() && base == "pitch") return s.pitch;
    } else if (component == "audiolistener" && registry.audioListeners.has(entity)) {
        if (axis.empty() && base == "gain") return registry.audioListeners.get(entity).gain;
    }
    return std::nullopt;
}

enum class Op { Lt, Le, Gt, Ge, Eq, Ne };

inline bool parseOp(const std::string& token, Op& op) {
    if (token == "<") { op = Op::Lt; return true; }
    if (token == "<=") { op = Op::Le; return true; }
    if (token == ">") { op = Op::Gt; return true; }
    if (token == ">=") { op = Op::Ge; return true; }
    if (token == "==" || token == "=") { op = Op::Eq; return true; }
    if (token == "!=") { op = Op::Ne; return true; }
    return false;
}

inline bool compare(float lhs, Op op, float rhs) {
    switch (op) {
        case Op::Lt: return lhs < rhs;
        case Op::Le: return lhs <= rhs;
        case Op::Gt: return lhs > rhs;
        case Op::Ge: return lhs >= rhs;
        case Op::Eq: return lhs == rhs;
        case Op::Ne: return lhs != rhs;
    }
    return false;
}

inline Result run(const Registry& registry, const std::string& query) {
    Result result;

    std::istringstream stream(query);
    std::vector<std::string> tokens;
    std::string word;
    while (stream >> word) {
        tokens.push_back(word);
    }
    if (tokens.empty()) {
        result.error = "Enter a query, e.g. \"rigidbody where vel.y < 0\"";
        return result;
    }

    const std::string component = normalizeComponent(toLower(tokens[0]));
    std::vector<Entity> candidates;
    if (!collectEntitiesWith(registry, component, candidates)) {
        result.error = "Unknown component: " + tokens[0];
        return result;
    }

    if (tokens.size() == 1) {
        result.entities = std::move(candidates);
        return result;
    }

    if (tokens.size() != 5 || toLower(tokens[1]) != "where") {
        result.error = "Syntax: <component> [where <field> <op> <value>]";
        return result;
    }

    const std::string field = toLower(tokens[2]);
    Op op{};
    if (!parseOp(tokens[3], op)) {
        result.error = "Unknown operator: " + tokens[3];
        return result;
    }
    float value = 0.0f;
    try {
        value = std::stof(tokens[4]);
    } catch (...) {
        result.error = "Not a number: " + tokens[4];
        return result;
    }

    bool anyFieldKnown = false;
    for (Entity entity : candidates) {
        const std::optional<float> v = fieldValue(registry, entity, component, field);
        if (v.has_value()) {
            anyFieldKnown = true;
            if (compare(*v, op, value)) {
                result.entities.push_back(entity);
            }
        }
    }
    if (!anyFieldKnown && !candidates.empty()) {
        result.error = "Unknown field '" + field + "' for " + component;
    }
    return result;
}

} // namespace EntityQuery

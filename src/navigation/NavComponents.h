#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

// Phase 18A — the authoritative half of navigation. See docs/DESIGN_NAVIGATION.md.

// Whether the agent is walking, done, or stuck.
//
// Authoritative, and that is the record's least obvious call: `Unreachable` is not a
// cosmetic label, it is the memory that a plan was **attempted**. Without it an agent
// with an impossible goal re-runs A* over the whole mesh every fixed step forever,
// and a snapshot restore silently restarts that attempt — so the same frame does
// different work in two runs. Structurally the same call DESIGN_ANIMATION.md makes
// for "which animation events already fired": the record of an attempt is state, even
// when the attempt itself is a pure function.
enum class NavAgentStatus {
    Idle,        // no plan; a destination will be planned for on the next step
    Following,   // walking `path`
    Arrived,     // reached the destination; will not replan until it changes
    Unreachable  // planning failed; will not retry until the destination changes
};

struct NavAgentComponent {
    // Navmesh name, resolved through NavMeshRegistry. A name (not a pointer or an
    // index) because it serializes, survives a snapshot as a plain string, and lets
    // a re-baked mesh be swapped underneath a walking agent without anything
    // dangling. Same reasoning as AnimationPlayerComponent::clip (RULES.md Rule 21a).
    std::string navMesh;

    // Gameplay's decision — where this agent wants to be. Navigation reads it and
    // never writes it: *where to go* is a gameplay concept the navigation system
    // executes, not one it owns.
    glm::vec3 destination = glm::vec3(0.0f);
    bool hasDestination = false;

    // **The plan, and it is authoritative** — the one classification this subsystem
    // exists to get right. A path looks like a cache of `f(navmesh, position, goal)`,
    // but it is a function of where the agent stood *when it planned*: at a corridor
    // fork, an agent that took the left route and an agent replanning from halfway
    // down it can legitimately disagree. A pose is a function of the present; a path
    // is a function of the past, and the present cannot reconstruct the past.
    //
    // Excludes the agent's own position and ends at the destination — the places to
    // walk to, not a polyline of where it has been.
    std::vector<glm::vec3> path;
    int pathIndex = 0;

    // The destination `path` was planned for, so deciding whether to replan is a
    // comparison against authoritative state rather than a `dirty` flag every
    // behavior that moves a destination has to remember to set. A forgotten flag is
    // an agent walking confidently to the wrong place; one extra vec3 removes the
    // whole class (RULES.md Rule 7). Compared exactly, not by distance: the question
    // is "did anyone write this field", not "how far did it move".
    glm::vec3 pathGoal = glm::vec3(0.0f);

    NavAgentStatus status = NavAgentStatus::Idle;

    // Metres per second along the path. Gameplay mutates it (hasted, wounded), so it
    // is game state rather than a tuning constant.
    float speed = 3.0f;

    // How close to the final waypoint counts as there. Authoritative for the same
    // reason speed is: a guard and a housefly do not agree about "arrived".
    float arrivalRadius = 0.1f;

    // The agent's own size, used by local avoidance (18D) to keep clearance from
    // obstacles. Distinct from NavBakeParams::agentRadius, which erodes the *mesh*
    // before planning — this one only affects steering within an already-planned
    // corridor. Two radii because they answer two different questions: "where may I
    // plan?" and "how close may I pass?".
    float radius = 0.25f;

    // Re-arms planning for `target`. Setting `destination` directly works too, but
    // only when the value actually changes — this also re-plans a *repeat* of a
    // destination that previously came back Unreachable, which is the one case a
    // pure comparison cannot see.
    void setDestination(const glm::vec3& target) {
        destination = target;
        hasDestination = true;
        status = NavAgentStatus::Idle;
    }

    void clearDestination() {
        hasDestination = false;
        status = NavAgentStatus::Idle;
        path.clear();
        pathIndex = 0;
    }
};

// Phase 18B — marks this entity's mesh as **input geometry** for a named navmesh.
//
// It names the navmesh it feeds, which is what closes the RULES.md Rule 21a loop for
// navigation. `NavAgentComponent::navMesh` is a name, and Rule 21a demands something
// reconstruct that asset from the name alone on scene load. For clips and skins the
// answer was "re-read the model file"; here it is better — **the scene itself carries
// its own bake inputs**, so a loaded scene can rebuild its navmesh from nothing but
// the entities it just created.
//
// The consequence, which is what makes navigation different from every other
// asset-backed component: a navmesh is derived from the **whole scene**, not from one
// file, so it cannot be reconstituted per-entity the way
// ModelImporter::ensureModelAssets is. It has to happen *after* every entity exists
// and is parented — a post-load step, not an inline one.
struct NavMeshSourceComponent {
    std::string navMesh; // which navmesh this geometry contributes to
};

// Phase 18D — a transient obstacle agents steer around without replanning.
//
// **Deliberately not part of the navmesh.** Baked geometry defines where an agent
// *may* go; an obstacle is a condition it encounters while going there. Folding
// obstacles into the mesh would mean rebaking whenever one moved, and would let a
// moving crate silently redefine which corridor the planner chose.
//
// The position is the entity's ordinary TransformComponent — no second copy.
struct NavObstacleComponent {
    float radius = 0.5f;
};

// Serialized as a string, like ColliderType — an integer would make the scene file
// depend on enumerator order, so inserting a status would silently reinterpret every
// saved agent.
inline const char* navAgentStatusName(NavAgentStatus status) {
    switch (status) {
        case NavAgentStatus::Following:   return "following";
        case NavAgentStatus::Arrived:     return "arrived";
        case NavAgentStatus::Unreachable: return "unreachable";
        case NavAgentStatus::Idle:        break;
    }
    return "idle";
}

inline NavAgentStatus navAgentStatusFromName(const std::string& name) {
    if (name == "following")   return NavAgentStatus::Following;
    if (name == "arrived")     return NavAgentStatus::Arrived;
    if (name == "unreachable") return NavAgentStatus::Unreachable;
    return NavAgentStatus::Idle;
}

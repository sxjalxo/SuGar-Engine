#include "navigation/NavigationSystem.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "ecs/Registry.h"
#include "navigation/NavComponents.h"
#include "navigation/NavMesh.h"
#include "navigation/NavMeshRegistry.h"
#include "navigation/NavPath.h"

namespace {

// Plans a fresh route for `agent` from `position`. Writes the plan, the goal it was
// planned for, and the resulting status — all authoritative, all in the component.
void planRoute(NavAgentComponent& agent, const glm::vec3& position) {
    const NavMesh* mesh = NavMeshRegistry::get(agent.navMesh);
    if (mesh == nullptr) {
        // Unknown navmesh name — left Idle rather than marked Unreachable, so the
        // agent plans as soon as the mesh appears. A mesh may simply not be baked
        // yet, and "the bake has not run" is not the same fact as "no route exists".
        // Same call AnimationSystem makes for an unresolved clip.
        return;
    }

    // Planned for `destination`, not for the snapped point the search actually used:
    // the replan check compares against what gameplay wrote, so recording anything
    // else would make the agent replan every single step.
    agent.pathGoal = agent.destination;
    agent.pathIndex = 0;

    const NavPath::Result result = NavPath::findPath(*mesh, position, agent.destination, agent.path);
    if (result != NavPath::Result::Success) {
        agent.path.clear();
        agent.status = NavAgentStatus::Unreachable;
        return;
    }

    agent.status = NavAgentStatus::Following;

    // Deliberately *not* snapping the agent onto the mesh here. findPath plans from
    // the nearest on-mesh point, so an agent standing slightly off the surface walks
    // back onto it over the next few steps; teleporting it on the frame a
    // destination is set would be a visible pop that no gameplay code asked for.
}

// An obstacle as steering sees it: a position and a radius, nothing else.
struct Obstacle {
    glm::vec3 position{0.0f};
    float radius = 0.0f;
};

// Is anything close enough to steer around? Cheap test so an uncrowded scene stays
// on the exact path-following code path (and its 18A tests) untouched.
bool anyObstacleNear(const glm::vec3& position, float agentRadius,
                     const std::vector<Obstacle>& obstacles) {
    for (const Obstacle& obstacle : obstacles) {
        const float clearance = obstacle.radius + agentRadius;
        const float dx = position.x - obstacle.position.x;
        const float dz = position.z - obstacle.position.z;
        if ((dx * dx + dz * dz) < clearance * clearance) {
            return true;
        }
    }
    return false;
}

// Sum of the steering pushes from every obstacle overlapping the agent's clearance.
//
// **Radial alone is not enough, and this is the bug the phase actually found.** A
// purely repulsive push points straight back along the approach, so an obstacle
// squarely between an agent and its waypoint produces `desired + push == 0`: the
// agent stops dead a clearance-width short and never arrives. Repulsion answers
// "get away from it" when the question is "get *around* it".
//
// So each obstacle contributes a radial term *and* a tangential one — the direction
// along the obstacle's edge, sided toward the goal. The tangent is what converts a
// standoff into an orbit.
//
// **Purely a function of the present** — obstacle positions, agent position, radii,
// desired direction. Nothing is remembered between steps, so by RULES.md Rule 21b it
// is derived and needs no ECS presence: recomputing it from today's state cannot
// produce a different valid answer, because it depends on no past input.
//
// The moment that stops being true it becomes authoritative. If reciprocal
// oscillation ever needs a remembered "preferred side" (two agents dancing in a
// doorway), that preference *is* history and must be an ECS field, not a private
// member — Rule 21b's bug in the shape it usually arrives.
glm::vec3 avoidanceOffset(const glm::vec3& position, float agentRadius,
                          const glm::vec3& desired, const std::vector<Obstacle>& obstacles) {
    glm::vec3 push(0.0f);

    for (const Obstacle& obstacle : obstacles) {
        const float clearance = obstacle.radius + agentRadius;
        glm::vec3 away = position - obstacle.position;
        away.y = 0.0f; // steering is a ground-plane concern, like every other query

        const float distanceSquared = away.x * away.x + away.z * away.z;
        if (distanceSquared >= clearance * clearance) {
            continue;
        }

        const float distance = std::sqrt(distanceSquared);
        glm::vec3 radial(0.0f);
        if (distance < 1e-5f) {
            // Dead centre: no meaningful direction to flee. A fixed axis, never a
            // random one — a random nudge would make the sim non-reproducible for
            // the one case that most needs to reproduce (Rule 10).
            radial = glm::vec3(1.0f, 0.0f, 0.0f);
        } else {
            radial = away / distance;
        }

        // Tangent in the ground plane, sided toward the goal so the detour makes
        // progress rather than orbiting away from it. On an exact tie (obstacle dead
        // ahead) the sign is fixed, not arbitrary — same determinism argument.
        glm::vec3 tangent(-radial.z, 0.0f, radial.x);
        if ((tangent.x * desired.x + tangent.z * desired.z) < 0.0f) {
            tangent = -tangent;
        }

        // Linear falloff: full strength at the centre, nothing at the rim, so an
        // agent brushing the edge is nudged rather than jerked.
        const float strength = clearance - distance;
        push += (radial + tangent) * strength;
    }

    return push;
}

// Steers `agent` one step around nearby obstacles, deflecting *within* its corridor.
//
// It never touches `path` or `pathIndex`. Avoidance changes **how** an agent
// traverses the route it planned, not **which** route that is: an agent that steps
// aside for a moving crate is still following the same corridor and rejoins it once
// the crate is gone. Letting avoidance edit the path would let a transient condition
// silently redefine a planning decision, and the two have completely different
// lifetimes.
void steerAroundObstacles(NavAgentComponent& agent, glm::vec3& position, float dt,
                          const std::vector<Obstacle>& obstacles) {
    const glm::vec3 target = agent.path[static_cast<std::size_t>(agent.pathIndex)];
    glm::vec3 toTarget = target - position;
    toTarget.y = 0.0f;

    const float distance = std::sqrt(toTarget.x * toTarget.x + toTarget.z * toTarget.z);
    if (distance < 1e-5f) {
        return;
    }

    const glm::vec3 desired = toTarget / distance;
    const glm::vec3 push = avoidanceOffset(position, agent.radius, desired, obstacles);

    // Desired direction plus the obstacle push, renormalized. The agent still moves
    // at its own speed — avoidance changes heading, not pace, so a crowded corridor
    // slows nobody down in a way gameplay did not ask for.
    glm::vec3 heading = desired + push;
    heading.y = 0.0f;
    const float headingLength = std::sqrt(heading.x * heading.x + heading.z * heading.z);
    if (headingLength < 1e-5f) {
        return; // push exactly cancels the goal; hold position this step
    }

    const float budget = agent.speed * dt;
    const glm::vec3 step = (heading / headingLength) * budget;
    // Never overshoot the waypoint while deflected — that would let avoidance skip
    // part of the route it is supposed to be following.
    position += (budget > distance) ? (toTarget / distance) * distance : step;

    // Waypoint advance still uses the ordinary arrival test, so progress along the
    // corridor is decided the same way with or without obstacles present.
    glm::vec3 remaining = target - position;
    remaining.y = 0.0f;
    const bool isFinal = (agent.pathIndex + 1 == static_cast<int>(agent.path.size()));
    const float threshold = isFinal ? agent.arrivalRadius : 1e-3f;
    if (std::sqrt(remaining.x * remaining.x + remaining.z * remaining.z) <= threshold) {
        agent.pathIndex++;
    }

    if (agent.pathIndex >= static_cast<int>(agent.path.size())) {
        agent.status = NavAgentStatus::Arrived;
        agent.path.clear();
        agent.pathIndex = 0;
    }
}

// Walks `agent` along its path, spending the whole step's travel budget.
void followPath(NavAgentComponent& agent, glm::vec3& position, float dt) {
    float budget = agent.speed * dt;
    if (!(budget > 0.0f)) {
        return;
    }

    // A loop, not a single step-and-maybe-advance. "Move toward the next waypoint,
    // then advance if we reached it" silently caps an agent at one waypoint per
    // frame, so a fast agent crawls through a cluster of close waypoints. Same shape
    // as 17A's loop-wrap bug (`time -= duration` breaks the moment one step
    // overshoots the clip): spend the budget, don't assume one step crosses at most
    // one boundary.
    while (agent.pathIndex < static_cast<int>(agent.path.size()) && budget > 0.0f) {
        const glm::vec3 target = agent.path[static_cast<std::size_t>(agent.pathIndex)];
        const float distance = glm::distance(position, target);
        const bool isFinal = (agent.pathIndex + 1 == static_cast<int>(agent.path.size()));

        if (distance <= 1e-6f) {
            agent.pathIndex++;
            continue;
        }

        // Close enough to the *destination* counts as there — and without moving, so
        // arrivalRadius reads as a tolerance rather than as a snap.
        if (isFinal && distance <= agent.arrivalRadius) {
            agent.pathIndex++;
            break;
        }

        if (distance <= budget) {
            position = target;
            budget -= distance;
            agent.pathIndex++;
            continue;
        }

        position += (target - position) * (budget / distance);
        budget = 0.0f;
    }

    if (agent.pathIndex >= static_cast<int>(agent.path.size())) {
        agent.status = NavAgentStatus::Arrived;

        // The plan is spent. Cleared rather than kept so a restored snapshot can
        // never show an arrived agent still carrying the route it walked — one fewer
        // way for two pieces of authoritative state to disagree.
        agent.path.clear();
        agent.pathIndex = 0;
    }
}

} // namespace

namespace NavigationSystem {

void update(Registry& registry, float dt) {
    // Collected once per step and **sorted by entity id**. Sorting is not tidiness:
    // avoidance sums a push per obstacle, float addition is not associative, and
    // ComponentStorage iterates an unordered_map — so an unsorted sum would make the
    // result depend on hash order. Deterministic by construction rather than by
    // accident (Rule 10).
    //
    // Read through a **const** Registry view: ComponentStorage's non-const getAll()
    // records a *write*, so iterating obstacles off the mutable registry would make
    // this system mutate a storage it only declared as a read — and the Phase 13B
    // enforcement rightly rejects that. Same idiom CollisionDispatch uses. The guard
    // rail caught this exact mistake here.
    const Registry& readOnly = registry;

    std::vector<std::pair<Entity, Obstacle>> obstacleList;
    obstacleList.reserve(readOnly.navObstacles.getAll().size());
    for (const auto& [entity, obstacle] : readOnly.navObstacles.getAll()) {
        if (!readOnly.transforms.has(entity) || !(obstacle.radius > 0.0f)) {
            continue;
        }
        obstacleList.push_back({ entity, Obstacle{ getWorldPosition(entity, readOnly), obstacle.radius } });
    }
    std::sort(obstacleList.begin(), obstacleList.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<Obstacle> obstacles;
    obstacles.reserve(obstacleList.size());
    for (const auto& [entity, obstacle] : obstacleList) {
        (void)entity;
        obstacles.push_back(obstacle);
    }

    for (auto& [entity, agent] : registry.navAgents.getAll()) {
        if (!registry.transforms.has(entity)) {
            continue; // nothing to move
        }

        if (!agent.hasDestination) {
            if (agent.status != NavAgentStatus::Idle) {
                agent.clearDestination();
            }
            continue;
        }

        glm::vec3 position = registry.transforms.get(entity).transform.position;

        // Replan on Idle (a destination was just armed) or when gameplay moved the
        // destination out from under the current plan. Arrived and Unreachable are
        // both terminal until one of those happens — which is what stops a stuck
        // agent from burning a full A* every step, forever.
        if (agent.status == NavAgentStatus::Idle || agent.destination != agent.pathGoal) {
            planRoute(agent, position);
        }

        if (agent.status != NavAgentStatus::Following) {
            continue;
        }

        // Plan and move in the same step: an agent that planned on one step and
        // started moving on the next would carry a frame of latency that depends on
        // system order, which is exactly what the scheduler's declared access sets
        // exist to make visible rather than hide.
        // Avoidance engages only when something is actually in the way. With nothing
        // near, the agent stays on the exact path-following code path — which is what
        // keeps 18A's budget-consuming behaviour (and its tests) untouched by 18D.
        if (!obstacles.empty() && anyObstacleNear(position, agent.radius, obstacles)) {
            steerAroundObstacles(agent, position, dt, obstacles);
        } else {
            followPath(agent, position, dt);
        }
        registry.transforms.get(entity).transform.position = position;
    }
}

} // namespace NavigationSystem

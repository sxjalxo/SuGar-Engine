#include "physics/PhysicsWorld.h"
#include "ecs/Registry.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <glm/glm.hpp>

namespace {

// World-space collider derived from a transform. Boxes stay axis-aligned (v1
// ignores rotation); the collider's local extents/radius are scaled by the
// transform scale.
struct WorldShape {
    Entity entity = INVALID_ENTITY;
    ColliderType type = ColliderType::Box;
    glm::vec3 center{0.0f};
    glm::vec3 halfExtents{0.0f}; // box
    float radius = 0.0f;         // sphere
};

// Contact normal always points from shape A to shape B.
struct Contact {
    bool hit = false;
    glm::vec3 normal{0.0f};
    float penetration = 0.0f;
};

WorldShape makeWorldShape(Entity entity, const Transform& transform, const ColliderComponent& collider) {
    WorldShape shape;
    shape.entity = entity;
    shape.type = collider.type;
    shape.center = transform.position;
    shape.halfExtents = collider.halfExtents * glm::abs(transform.scale);
    const float maxScale = std::max({std::abs(transform.scale.x),
                                     std::abs(transform.scale.y),
                                     std::abs(transform.scale.z)});
    shape.radius = collider.radius * maxScale;
    return shape;
}

Contact testBoxBox(const WorldShape& a, const WorldShape& b) {
    Contact contact;
    const glm::vec3 aMin = a.center - a.halfExtents;
    const glm::vec3 aMax = a.center + a.halfExtents;
    const glm::vec3 bMin = b.center - b.halfExtents;
    const glm::vec3 bMax = b.center + b.halfExtents;

    const glm::vec3 overlap(
        std::min(aMax.x, bMax.x) - std::max(aMin.x, bMin.x),
        std::min(aMax.y, bMax.y) - std::max(aMin.y, bMin.y),
        std::min(aMax.z, bMax.z) - std::max(aMin.z, bMin.z));

    if (overlap.x <= 0.0f || overlap.y <= 0.0f || overlap.z <= 0.0f) {
        return contact;
    }

    // Resolve along the axis of least penetration.
    float penetration = overlap.x;
    glm::vec3 normal((b.center.x < a.center.x) ? -1.0f : 1.0f, 0.0f, 0.0f);
    if (overlap.y < penetration) {
        penetration = overlap.y;
        normal = glm::vec3(0.0f, (b.center.y < a.center.y) ? -1.0f : 1.0f, 0.0f);
    }
    if (overlap.z < penetration) {
        penetration = overlap.z;
        normal = glm::vec3(0.0f, 0.0f, (b.center.z < a.center.z) ? -1.0f : 1.0f);
    }

    contact.hit = true;
    contact.normal = normal;
    contact.penetration = penetration;
    return contact;
}

Contact testSphereSphere(const WorldShape& a, const WorldShape& b) {
    Contact contact;
    const glm::vec3 diff = b.center - a.center;
    const float dist = glm::length(diff);
    const float radiusSum = a.radius + b.radius;
    if (dist >= radiusSum) {
        return contact;
    }

    contact.hit = true;
    contact.penetration = radiusSum - dist;
    contact.normal = dist > 1e-6f ? diff / dist : glm::vec3(0.0f, 1.0f, 0.0f);
    return contact;
}

// box vs sphere, normal pointing from the box toward the sphere.
Contact testBoxSphere(const WorldShape& box, const WorldShape& sphere) {
    Contact contact;
    const glm::vec3 boxMin = box.center - box.halfExtents;
    const glm::vec3 boxMax = box.center + box.halfExtents;
    const glm::vec3 closest = glm::clamp(sphere.center, boxMin, boxMax);
    const glm::vec3 diff = sphere.center - closest;
    const float dist = glm::length(diff);

    if (dist > 1e-6f) {
        if (dist >= sphere.radius) {
            return contact;
        }
        contact.hit = true;
        contact.normal = diff / dist;
        contact.penetration = sphere.radius - dist;
        return contact;
    }

    // Sphere center inside the box: push out along the nearest face.
    const glm::vec3 toMax = boxMax - sphere.center;
    const glm::vec3 toMin = sphere.center - boxMin;
    float penetration = toMax.x;
    glm::vec3 normal(1.0f, 0.0f, 0.0f);
    auto consider = [&](float depth, const glm::vec3& axis) {
        if (depth < penetration) {
            penetration = depth;
            normal = axis;
        }
    };
    consider(toMin.x, glm::vec3(-1.0f, 0.0f, 0.0f));
    consider(toMax.y, glm::vec3(0.0f, 1.0f, 0.0f));
    consider(toMin.y, glm::vec3(0.0f, -1.0f, 0.0f));
    consider(toMax.z, glm::vec3(0.0f, 0.0f, 1.0f));
    consider(toMin.z, glm::vec3(0.0f, 0.0f, -1.0f));

    contact.hit = true;
    contact.normal = normal;
    contact.penetration = penetration + sphere.radius;
    return contact;
}

Contact narrowphase(const WorldShape& a, const WorldShape& b) {
    if (a.type == ColliderType::Box && b.type == ColliderType::Box) {
        return testBoxBox(a, b);
    }
    if (a.type == ColliderType::Sphere && b.type == ColliderType::Sphere) {
        return testSphereSphere(a, b);
    }
    if (a.type == ColliderType::Box && b.type == ColliderType::Sphere) {
        return testBoxSphere(a, b); // normal box(a) -> sphere(b) == a -> b
    }

    // a is sphere, b is box: compute box->sphere then flip to a->b.
    Contact contact = testBoxSphere(b, a);
    contact.normal = -contact.normal;
    return contact;
}

// These three only inspect bodies, so they take a const Registry: the ECS records
// access through the const path as a read, which is what lets the Physics system
// prove it doesn't mutate anything it declared read-only (Phase 13B).
float inverseMass(const Registry& registry, Entity entity) {
    if (!registry.rigidBodies.has(entity)) {
        return 0.0f; // collider with no body == immovable
    }
    const RigidBodyComponent& body = registry.rigidBodies.get(entity);
    if (body.isStatic || body.mass <= 0.0001f) {
        return 0.0f;
    }
    return 1.0f / body.mass;
}

float restitutionOf(const Registry& registry, Entity entity) {
    return registry.rigidBodies.has(entity) ? registry.rigidBodies.get(entity).restitution : 0.0f;
}

float frictionOf(const Registry& registry, Entity entity) {
    return registry.rigidBodies.has(entity) ? registry.rigidBodies.get(entity).friction : 0.0f;
}

// Resolves one contact and returns the normal impulse magnitude (0 if the bodies
// are immovable or already separating), which callers surface as the collision
// event's "impact strength".
float resolveContact(Registry& registry, const WorldShape& a, const WorldShape& b, const Contact& contact) {
    const float invA = inverseMass(registry, a.entity);
    const float invB = inverseMass(registry, b.entity);
    const float invSum = invA + invB;
    if (invSum <= 0.0f) {
        return 0.0f; // both immovable
    }

    // Positional correction so resting bodies don't sink (Baumgarte with slop).
    constexpr float correctionPercent = 0.8f;
    constexpr float slop = 0.01f;
    const glm::vec3 correction =
        (std::max(contact.penetration - slop, 0.0f) / invSum) * correctionPercent * contact.normal;
    if (invA > 0.0f) {
        registry.transforms.get(a.entity).transform.position -= invA * correction;
    }
    if (invB > 0.0f) {
        registry.transforms.get(b.entity).transform.position += invB * correction;
    }

    // Normal impulse with restitution.
    const glm::vec3 vA = registry.rigidBodies.has(a.entity) ? registry.rigidBodies.get(a.entity).velocity : glm::vec3(0.0f);
    const glm::vec3 vB = registry.rigidBodies.has(b.entity) ? registry.rigidBodies.get(b.entity).velocity : glm::vec3(0.0f);
    const glm::vec3 relativeVelocity = vB - vA;
    const float velAlongNormal = glm::dot(relativeVelocity, contact.normal);
    if (velAlongNormal > 0.0f) {
        return 0.0f; // already separating
    }

    const float e = std::min(restitutionOf(registry, a.entity), restitutionOf(registry, b.entity));
    const float j = -(1.0f + e) * velAlongNormal / invSum;
    glm::vec3 totalImpulse = j * contact.normal;

    // Coulomb friction: a tangential impulse opposing sliding, clamped by the
    // normal impulse (|jt| <= mu * j). mu is the combined friction coefficient.
    glm::vec3 tangent = relativeVelocity - velAlongNormal * contact.normal;
    const float tangentLength = glm::length(tangent);
    if (tangentLength > 1e-6f) {
        tangent /= tangentLength;
        const float jt = -glm::dot(relativeVelocity, tangent) / invSum;
        const float mu = std::sqrt(frictionOf(registry, a.entity) * frictionOf(registry, b.entity));
        const float clampedJt = std::clamp(jt, -mu * j, mu * j);
        totalImpulse += clampedJt * tangent;
    }

    if (invA > 0.0f) {
        registry.rigidBodies.get(a.entity).velocity = vA - invA * totalImpulse;
    }
    if (invB > 0.0f) {
        registry.rigidBodies.get(b.entity).velocity = vB + invB * totalImpulse;
    }

    return j; // normal impulse magnitude (>= 0 here)
}

} // namespace

void PhysicsWorld::step(Registry& registry, float deltaTime) {
    collisionEvents.clear();

    // 1) Integrate every dynamic body (semi-implicit Euler).
    for (auto& [entity, body] : registry.rigidBodies.getAll()) {
        if (body.isStatic || !registry.transforms.has(entity)) {
            body.accumulatedForce = glm::vec3(0.0f);
            continue;
        }

        const float safeMass = body.mass > 0.0001f ? body.mass : 0.0001f;
        glm::vec3 acceleration = body.accumulatedForce / safeMass;
        if (body.useGravity) {
            acceleration += gravity;
        }

        body.velocity += acceleration * deltaTime;
        registry.transforms.get(entity).transform.position += body.velocity * deltaTime;
        body.accumulatedForce = glm::vec3(0.0f);
    }

    // 2) Broadphase: gather world shapes (all-pairs; fine for small scenes, a
    //    uniform grid / sweep-and-prune can replace this later). Read through a
    //    const view so colliders are recorded as a read, not a write — physics
    //    declares Collider read-only (Phase 13B).
    const Registry& readOnly = registry;
    std::vector<WorldShape> shapes;
    shapes.reserve(readOnly.colliders.getAll().size());
    for (const auto& [entity, collider] : readOnly.colliders.getAll()) {
        if (!readOnly.transforms.has(entity)) {
            continue;
        }
        shapes.push_back(makeWorldShape(entity, readOnly.transforms.get(entity).transform, collider));
    }

    // 3) Narrowphase + resolution. Every hit becomes a CollisionEvent so gameplay
    //    (behaviors) can react; the contact point is approximated as the midpoint
    //    of the two centers (good enough for triggers/sfx; refine if needed).
    for (size_t i = 0; i < shapes.size(); ++i) {
        for (size_t k = i + 1; k < shapes.size(); ++k) {
            const Contact contact = narrowphase(shapes[i], shapes[k]);
            if (!contact.hit) {
                continue;
            }

            const float impulse = resolveContact(registry, shapes[i], shapes[k], contact);

            CollisionEvent event;
            event.a = shapes[i].entity;
            event.b = shapes[k].entity;
            event.point = 0.5f * (shapes[i].center + shapes[k].center);
            event.normal = contact.normal;
            event.impulse = impulse;
            collisionEvents.push_back(event);
        }
    }
}

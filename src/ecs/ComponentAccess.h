#pragma once

#include <cstdint>
#include <string>

// Phase 13B — architecture guard rails (Pillar 4). Phase 13A let systems *declare*
// the component storages they read and write; nothing verified the declaration.
// This header is the verification machinery: ComponentStorage reports every access
// to the thread's active ComponentAccessTracker, and SystemScheduler compares what
// a system actually touched against what it declared. A system reaching into a
// storage it never declared is hidden coupling — exactly the pattern the engine
// should make hard to write.
//
// Cost: the recording calls compile away entirely unless SUGAR_ACCESS_TRACKING is
// defined (Debug builds only, set by CMake on the Core target as PUBLIC so the
// engine, the game DLL, and Core all agree). Release keeps the bare storage ops.

// One bit per component storage in Registry. Kept in lockstep with Registry's
// storages; a system declares the ones it touches so the scheduler can tell
// disjoint systems (parallelizable) from conflicting ones (must be ordered), and
// so undeclared access can be rejected.
enum class ComponentType : uint32_t {
    Name = 0,
    Transform,
    Mesh,
    Material,
    Hierarchy,
    Script,
    RigidBody,
    Collider,
    PrefabInstance,
    AudioSource,
    AudioListener,
    UIScreen,
    Focus,
    Count
};

// A small bitset over ComponentType. 11 components fit comfortably in 32 bits.
using ComponentMask = uint32_t;

inline constexpr ComponentMask componentBit(ComponentType type) {
    return ComponentMask{1} << static_cast<uint32_t>(type);
}

// Fold a pack of ComponentType values into a single mask, e.g.
// maskOf(ComponentType::Transform, ComponentType::RigidBody).
inline ComponentMask maskOf() { return 0; }

template <typename... Rest>
inline ComponentMask maskOf(ComponentType first, Rest... rest) {
    return componentBit(first) | maskOf(rest...);
}

const char* componentTypeName(ComponentType type);
// "Transform|RigidBody" — for violation messages. "<none>" when empty.
std::string describeComponentMask(ComponentMask mask);

// Maps a component struct to its ComponentType. Specialized once per storage
// (see Registry.h); anything unspecialized is simply not tracked, so
// ComponentStorage still works for non-registry component types.
template <typename T>
struct ComponentTraits {
    static constexpr bool tracked = false;
    static constexpr ComponentType type = ComponentType::Count;
};

// Accumulates the storages accessed inside a scope. `touched` is every storage
// read or written; `mutated` is the subset reached through a mutating (non-const)
// path. The split matters: touching an undeclared storage at all is coupling,
// while mutating one you only declared as a read is a stronger violation.
class ComponentAccessTracker {
public:
    void recordRead(ComponentType type) {
        touchedMask |= componentBit(type);
    }

    void recordWrite(ComponentType type) {
        const ComponentMask bit = componentBit(type);
        touchedMask |= bit;
        mutatedMask |= bit;
    }

    ComponentMask touched() const { return touchedMask; }
    ComponentMask mutated() const { return mutatedMask; }

    void reset() {
        touchedMask = 0;
        mutatedMask = 0;
    }

private:
    ComponentMask touchedMask = 0;
    ComponentMask mutatedMask = 0;
};

namespace ComponentAccess {

// The tracker installed for the calling thread, or null when tracking is off
// (the default). Defined in Core's .cpp — never an inline/header variable — so
// the engine exe, Core, and the game DLL all share exactly one instance, the same
// reason BehaviorRegistry's table lives in a .cpp.
ComponentAccessTracker* activeTracker();
void setActiveTracker(ComponentAccessTracker* tracker);

// Recording hooks used by ComponentStorage. Outside Debug these are empty and the
// storage ops keep their original codegen.
template <typename T>
inline void recordRead() {
#ifdef SUGAR_ACCESS_TRACKING
    if constexpr (ComponentTraits<T>::tracked) {
        if (ComponentAccessTracker* tracker = activeTracker()) {
            tracker->recordRead(ComponentTraits<T>::type);
        }
    }
#endif
}

template <typename T>
inline void recordWrite() {
#ifdef SUGAR_ACCESS_TRACKING
    if constexpr (ComponentTraits<T>::tracked) {
        if (ComponentAccessTracker* tracker = activeTracker()) {
            tracker->recordWrite(ComponentTraits<T>::type);
        }
    }
#endif
}

// True when the build can observe component access at all. Enforcement is a
// development-time guard rail, not a release-time check.
inline constexpr bool trackingEnabled() {
#ifdef SUGAR_ACCESS_TRACKING
    return true;
#else
    return false;
#endif
}

// Installs `tracker` for the enclosing scope and restores the previous one on
// exit, so nested scopes (a system that calls into another) don't lose state.
class Scope {
public:
    explicit Scope(ComponentAccessTracker* tracker) : previous(activeTracker()) {
        setActiveTracker(tracker);
    }
    ~Scope() { setActiveTracker(previous); }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    ComponentAccessTracker* previous;
};

} // namespace ComponentAccess

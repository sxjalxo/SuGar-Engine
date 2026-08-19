#pragma once

#include <cstdint>
#include <vector>
#include "ecs/Entity.h"

// Allocates and recycles entity handles (DevDocs/DESIGN_GENERATIONAL_IDS.md).
//
// This is the only place in the engine that does arithmetic on an Entity. Everything
// else treats a handle as opaque, which is what made the move from a bare counter to
// a packed index/generation a change to one file rather than to the codebase.
//
// The model: one SLOT per index. A slot remembers how many tenants it has had
// (`generation`) and whether it currently has one (`alive`). Allocation takes a slot
// off the free list, bumps its generation and hands back index+generation packed
// together; destruction clears the alive flag and returns the index to the free list.
// A handle to a previous tenant therefore mismatches, and isAlive() can say so.
class EntityManager {
public:
    // A fresh entity. Returns INVALID_ENTITY only if the 20-bit index space is
    // exhausted (1 048 575 simultaneous entities — 368x the most any game has held);
    // callers that never checked the old counter's return value are no worse off,
    // since INVALID_ENTITY is already the "no entity" value they compare against.
    Entity createEntity();

    // Recreates a specific handle EXACTLY, generation included (Phase 14B). Used when
    // a destroyed subtree is restored (delete-undo / duplicate-redo) so it comes back
    // with its *original* ids — which is what lets editor commands keep raw ids
    // without a remap layer.
    //
    // This deliberately re-validates a handle the generation guard would otherwise
    // call stale: the destroy that preceded the undo already bumped the slot. That is
    // the design decision, not an oversight — the guard protects GAMEPLAY reuse, while
    // the editor's undo stack is a time machine, and restoring a subtree restores its
    // *identity*, not merely equivalent objects. The consequence is that a gameplay
    // handle captured before an editor delete becomes valid again after the undo,
    // which is exactly the pre-generational behaviour, confined to the editor path.
    //
    // The index must be free (previously destroyed, or beyond the allocated range);
    // any lower indices skipped to reach it are banked on the free list.
    Entity createEntityWithId(Entity id);

    void destroyEntity(Entity entity);

    // True if `entity` is the handle of the slot's CURRENT tenant. The payoff of the
    // packing, and something the engine could not answer before at any price: a
    // recycled bare integer was indistinguishable from the original.
    bool isAlive(Entity entity) const;

    void reset();

private:
    // One entry per index; slots[0] is the reserved INVALID_ENTITY slot and is never
    // handed out. `generation` is 0 until the slot is first allocated, so a zeroed
    // handle can never match a live slot.
    struct Slot {
        Entity generation = 0;
        bool alive = false;
    };

    // Next never-used index. Together with the free list this reproduces the old
    // counter's behaviour one level down: dense, sequential, LIFO reuse.
    Entity nextIndex = 1;
    std::vector<Slot> slots{ Slot{} }; // slots[0] reserved
    std::vector<Entity> freeIndices;
};

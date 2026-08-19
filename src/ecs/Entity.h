#pragma once

#include <cstdint>

// An entity handle: a 32-bit value packing a slot INDEX with a GENERATION counter
// (DevDocs/DESIGN_GENERATIONAL_IDS.md). The width is unchanged from the plain
// counter this replaced; only its interpretation is.
//
//   bit 31 .............. 20 | 19 ................. 0
//   generation (12 bits)     | index (20 bits)
//
// The index says WHERE an entity lives — it is the thing the free list recycles and
// the only half that may ever address anything. The generation says WHICH tenant of
// that slot the handle refers to: it counts how many times the slot has been reused,
// so a handle kept past its entity's destruction no longer matches the slot and
// Registry::isAlive() can say so. Before this, a recycled id was indistinguishable
// from the original and the engine could not detect a stale handle at all.
//
// The split (20/12) is measured, not guessed: the highest live entity count any
// dogfood game reached is 2 846 against 1 048 575 available, and the arena's GPU
// torture reused one slot ~250 times in 90 seconds — which rules out an 8-bit
// generation and leaves 4 096 with ~16x headroom.
//
// Why 32 bits and not 64: GameDataComponent stores numbers as double, so a value
// above 2^53 cannot round-trip through a saved scene. The largest packed handle here
// is 4 294 967 295 — six orders of magnitude inside that guarantee.
using Entity = uint32_t;

inline constexpr uint32_t ENTITY_INDEX_BITS = 20;
inline constexpr uint32_t ENTITY_GENERATION_BITS = 12;
static_assert(ENTITY_INDEX_BITS + ENTITY_GENERATION_BITS == 32, "the packing must fill the handle");

inline constexpr Entity ENTITY_INDEX_MASK = (Entity{1} << ENTITY_INDEX_BITS) - 1;      // 0x000FFFFF
inline constexpr Entity ENTITY_GENERATION_MASK = (Entity{1} << ENTITY_GENERATION_BITS) - 1; // 0x00000FFF

// Index 0 and generation 0 are both reserved, which is what keeps INVALID_ENTITY at
// literal 0: every existing `== INVALID_ENTITY` check, every zero-initialised Entity
// member and every serialized absent-parent keeps working with no change. Live
// entities start at index 1, generation 1.
inline constexpr Entity MAX_ENTITY_INDEX = ENTITY_INDEX_MASK;           // 1 048 575 simultaneous
inline constexpr Entity MAX_ENTITY_GENERATION = ENTITY_GENERATION_MASK; // 4 095 reuses before wrap

constexpr Entity entityIndex(Entity entity) {
    return entity & ENTITY_INDEX_MASK;
}

constexpr Entity entityGeneration(Entity entity) {
    return (entity >> ENTITY_INDEX_BITS) & ENTITY_GENERATION_MASK;
}

constexpr Entity makeEntity(Entity index, Entity generation) {
    return (index & ENTITY_INDEX_MASK) | ((generation & ENTITY_GENERATION_MASK) << ENTITY_INDEX_BITS);
}

// Canonical order for anything that must iterate entities deterministically.
//
// Sorting the packed value directly would work — it is still a total order — but it
// would silently change what that order MEANS: a recycled low slot carrying a high
// generation would sort above a freshly allocated high slot, so "lowest id" would
// stop meaning "oldest slot". Several places depend on that meaning (the audio
// listener and game camera pick the lowest, and the scene serializer writes objects
// in this order, with `parent` referring to positions in the resulting array). Order
// by index and today's behaviour is preserved exactly; the generation is a tiebreak
// that can only matter for handles to different tenants of one slot.
constexpr bool entityOrderLess(Entity a, Entity b) {
    return entityIndex(a) != entityIndex(b) ? entityIndex(a) < entityIndex(b)
                                            : entityGeneration(a) < entityGeneration(b);
}

static constexpr Entity INVALID_ENTITY = 0;

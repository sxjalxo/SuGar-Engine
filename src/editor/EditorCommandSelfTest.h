#pragma once

// Opt-in self-test for the editor command infrastructure (Phase 11A). Exercises
// the parts that are hard to verify by clicking — transactions, compression, and
// entity remapping across recreate — on a throwaway Registry (no Vulkan needed,
// since these commands never touch ResourceManager). Run by setting the env var
// SUGAR_SELFTEST=1; prints one PASS/FAIL line per check.

#include <iostream>

#include "ecs/Registry.h"
#include "editor/EditorCommand.h"
#include "editor/EditorCommands.h"

namespace EditorCommandSelfTest {

inline Transform transformAtX(float x) {
    Transform t;
    t.position = glm::vec3(x, 0.0f, 0.0f);
    return t;
}

inline float xOf(Registry& registry, Entity entity) {
    return registry.transforms.get(entity).transform.position.x;
}

// A test-only command that emits a fixed remap from redo, to exercise the
// history's remap propagation without needing the serializer/ResourceManager.
class RemapEmitter : public EditorCommand {
public:
    explicit RemapEmitter(EntityRemap mapping) : mapping(std::move(mapping)) {}
    EntityRemap undo(Registry&) override { return {}; }
    EntityRemap redo(Registry&) override { return mapping; }

private:
    EntityRemap mapping;
};

inline bool check(const char* name, bool ok) {
    std::cout << "[selftest] " << (ok ? "PASS " : "FAIL ") << name << "\n";
    return ok;
}

inline bool run() {
    bool allOk = true;

    // 1) Transaction groups multiple edits into one undo step.
    {
        Registry reg;
        const Entity a = reg.createEntity();
        const Entity b = reg.createEntity();
        reg.transforms.add(a, { transformAtX(0.0f) });
        reg.transforms.add(b, { transformAtX(0.0f) });

        CommandHistory history;
        history.beginTransaction();
        reg.transforms.get(a).transform = transformAtX(1.0f);
        history.push(std::make_unique<TransformCommand>(a, transformAtX(0.0f), transformAtX(1.0f)));
        reg.transforms.get(b).transform = transformAtX(1.0f);
        history.push(std::make_unique<TransformCommand>(b, transformAtX(0.0f), transformAtX(1.0f)));
        history.commitTransaction(reg);

        allOk &= check("transaction is one entry", history.size() == 1);
        history.undo(reg);
        allOk &= check("transaction undo reverts all", xOf(reg, a) == 0.0f && xOf(reg, b) == 0.0f);
        history.redo(reg);
        allOk &= check("transaction redo reapplies all", xOf(reg, a) == 1.0f && xOf(reg, b) == 1.0f);
    }

    // 2) Compression merges back-to-back edits of the same entity.
    {
        Registry reg;
        const Entity a = reg.createEntity();
        reg.transforms.add(a, { transformAtX(0.0f) });

        CommandHistory history;
        reg.transforms.get(a).transform = transformAtX(1.0f);
        history.push(std::make_unique<TransformCommand>(a, transformAtX(0.0f), transformAtX(1.0f)));
        reg.transforms.get(a).transform = transformAtX(2.0f);
        history.push(std::make_unique<TransformCommand>(a, transformAtX(1.0f), transformAtX(2.0f)));

        allOk &= check("compression keeps one entry", history.size() == 1);
        history.undo(reg);
        allOk &= check("compressed undo restores original before", xOf(reg, a) == 0.0f);
    }

    // 3) Remap repoints an older command after a recreate reassigns ids.
    {
        Registry reg;
        const Entity a = reg.createEntity();
        const Entity b = reg.createEntity();
        reg.transforms.add(a, { transformAtX(0.0f) });
        reg.transforms.add(b, { transformAtX(0.0f) });

        CommandHistory history;
        reg.transforms.get(a).transform = transformAtX(1.0f);
        history.push(std::make_unique<TransformCommand>(a, transformAtX(0.0f), transformAtX(1.0f)));
        history.push(std::make_unique<RemapEmitter>(EntityRemap{ { a, b } }));

        // Walk to the bottom, then redo forward so the emitter fires its remap,
        // repointing the transform command from a -> b.
        history.undo(reg); // emitter
        history.undo(reg); // transform (a -> x=0)
        history.redo(reg); // transform (a -> x=1)
        history.redo(reg); // emitter -> remaps the transform command a->b

        history.undo(reg); // emitter (no-op)
        history.undo(reg); // transform now targets b -> sets b to its `before` (x=0)
        const bool remapped = xOf(reg, b) == 0.0f && xOf(reg, a) == 1.0f;
        allOk &= check("remap repoints command to new entity", remapped);
    }

    std::cout << "[selftest] " << (allOk ? "ALL PASS" : "FAILURES PRESENT") << "\n";
    return allOk;
}

} // namespace EditorCommandSelfTest

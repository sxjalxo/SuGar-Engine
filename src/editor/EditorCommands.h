#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ecs/Registry.h"
#include "editor/EditorCommand.h"
#include "scene/SceneSerializer.h"
#include "scene/Transform.h"

// Concrete editor commands. Kept header-only and included only by the editor
// translation unit (Renderer.cpp).

// Destroys an entity and all its descendants (children-first). Shared by the
// subtree create/delete commands.
inline void destroyEntitySubtree(Registry& registry, Entity root) {
    if (root == INVALID_ENTITY) {
        return;
    }
    std::vector<Entity> toDestroy;
    std::vector<Entity> stack{root};
    while (!stack.empty()) {
        const Entity current = stack.back();
        stack.pop_back();
        toDestroy.push_back(current);
        if (registry.hierarchy.has(current)) {
            for (Entity child : registry.hierarchy.get(current).children) {
                stack.push_back(child);
            }
        }
    }
    for (auto it = toDestroy.rbegin(); it != toDestroy.rend(); ++it) {
        registry.destroyEntity(*it);
    }
}

// A command defined by two closures. Used for small, self-contained edits (e.g.
// adding/removing a value-only component). It captures entity ids in its closures;
// since Phase 14B recreation preserves ids, those references stay valid across a
// destroy/recreate.
class LambdaCommand : public EditorCommand {
public:
    LambdaCommand(std::function<void(Registry&)> redoFn, std::function<void(Registry&)> undoFn)
        : redoFn(std::move(redoFn)), undoFn(std::move(undoFn)) {}

    void undo(Registry& registry) override { if (undoFn) undoFn(registry); }
    void redo(Registry& registry) override { if (redoFn) redoFn(registry); }

private:
    std::function<void(Registry&)> redoFn;
    std::function<void(Registry&)> undoFn;
};

// Groups several commands into one undo/redo step. (Transactions in
// CommandHistory are the preferred way to build these now, but this stays useful
// for callers that assemble a group up front.)
class CompositeCommand : public EditorCommand {
public:
    void add(std::unique_ptr<EditorCommand> command) { children.push_back(std::move(command)); }
    bool empty() const { return children.empty(); }
    size_t size() const { return children.size(); }

    void undo(Registry& registry) override {
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            (*it)->undo(registry);
        }
    }
    void redo(Registry& registry) override {
        for (auto& child : children) {
            child->redo(registry);
        }
    }

private:
    std::vector<std::unique_ptr<EditorCommand>> children;
};

// Transform edit from the gizmo or the inspector. The entity's id is stable across
// destroy/recreate (Phase 14B), so the stored reference stays valid; tryMerge
// coalesces back-to-back edits of the same entity into one step.
class TransformCommand : public EditorCommand {
public:
    TransformCommand(Entity entity, const Transform& before, const Transform& after)
        : entity(entity), before(before), after(after) {}

    void undo(Registry& registry) override { apply(registry, before); }
    void redo(Registry& registry) override { apply(registry, after); }

    bool tryMerge(const EditorCommand& next) override {
        const auto* other = dynamic_cast<const TransformCommand*>(&next);
        if (other != nullptr && other->entity == entity) {
            after = other->after; // keep our original `before`, take their final `after`
            return true;
        }
        return false;
    }

private:
    void apply(Registry& registry, const Transform& value) {
        if (registry.transforms.has(entity)) {
            registry.transforms.get(entity).transform = value;
        }
    }

    Entity entity;
    Transform before;
    Transform after;
};

// Hierarchy reparent from drag-and-drop.
class ReparentCommand : public EditorCommand {
public:
    ReparentCommand(Entity child, Entity oldParent, Entity newParent)
        : child(child), oldParent(oldParent), newParent(newParent) {}

    void undo(Registry& registry) override { trySetParent(registry, oldParent); }
    void redo(Registry& registry) override { trySetParent(registry, newParent); }

private:
    void trySetParent(Registry& registry, Entity parent) {
        try {
            registry.setParent(child, parent);
        } catch (...) {
        }
    }

    Entity child;
    Entity oldParent;
    Entity newParent;
};

// Creation of an entity subtree (duplicate). Stored serialized so redo
// re-instantiates through the same path as prefabs. `entities` are the subtree's
// ids in object order (root first), captured at first creation; redo recreates
// into those *same* ids (Phase 14B), so any other command referencing this
// subtree stays valid with no remap.
class CreateSubtreeCommand : public EditorCommand {
public:
    CreateSubtreeCommand(std::string serialized, Entity parent, std::vector<Entity> created)
        : serialized(std::move(serialized)), parent(parent), entities(std::move(created)) {}

    void undo(Registry& registry) override {
        destroyEntitySubtree(registry, root());
    }

    void redo(Registry& registry) override {
        const Entity newRoot = SceneSerializer::instantiateFromStringWithIds(registry, serialized, entities);
        if (newRoot != INVALID_ENTITY && parent != INVALID_ENTITY && registry.transforms.has(parent)) {
            try {
                registry.setParent(newRoot, parent);
            } catch (...) {
            }
        }
    }

private:
    Entity root() const { return entities.empty() ? INVALID_ENTITY : entities.front(); }

    std::string serialized;
    Entity parent;
    std::vector<Entity> entities;
};

// Deletion of an entity subtree — the mirror of CreateSubtreeCommand. The caller
// has already destroyed the subtree (after recording its serialized form and id
// order), so undo re-instantiates into the original ids and redo destroys again.
class DeleteSubtreeCommand : public EditorCommand {
public:
    DeleteSubtreeCommand(std::string serialized, Entity parent, std::vector<Entity> deletedOrder)
        : serialized(std::move(serialized)), parent(parent), entities(std::move(deletedOrder)) {}

    void undo(Registry& registry) override {
        const Entity newRoot = SceneSerializer::instantiateFromStringWithIds(registry, serialized, entities);
        if (newRoot != INVALID_ENTITY && parent != INVALID_ENTITY && registry.transforms.has(parent)) {
            try {
                registry.setParent(newRoot, parent);
            } catch (...) {
            }
        }
    }

    void redo(Registry& registry) override {
        destroyEntitySubtree(registry, root());
    }

private:
    Entity root() const { return entities.empty() ? INVALID_ENTITY : entities.front(); }

    std::string serialized;
    Entity parent;
    std::vector<Entity> entities;
};

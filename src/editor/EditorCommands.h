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

inline Entity remapped(const EntityRemap& mapping, Entity entity) {
    const auto it = mapping.find(entity);
    return it == mapping.end() ? entity : it->second;
}

// Builds an old->new id map by zipping two id lists that share an order (the
// serialization object order). Entries that didn't change are omitted.
inline EntityRemap buildRemap(const std::vector<Entity>& oldIds, const std::vector<Entity>& newIds) {
    EntityRemap mapping;
    const size_t count = std::min(oldIds.size(), newIds.size());
    for (size_t i = 0; i < count; ++i) {
        if (oldIds[i] != newIds[i]) {
            mapping.emplace(oldIds[i], newIds[i]);
        }
    }
    return mapping;
}

// A command defined by two closures. Used for small, self-contained edits (e.g.
// adding/removing a value-only component). NOTE: it captures entity ids in its
// closures, so it is not remap-aware — fine for edits on entities that aren't
// destroyed/recreated by other commands in the same history.
class LambdaCommand : public EditorCommand {
public:
    LambdaCommand(std::function<void(Registry&)> redoFn, std::function<void(Registry&)> undoFn)
        : redoFn(std::move(redoFn)), undoFn(std::move(undoFn)) {}

    EntityRemap undo(Registry& registry) override { if (undoFn) undoFn(registry); return {}; }
    EntityRemap redo(Registry& registry) override { if (redoFn) redoFn(registry); return {}; }

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

    EntityRemap undo(Registry& registry) override {
        EntityRemap combined;
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            merge(combined, (*it)->undo(registry));
        }
        return combined;
    }
    EntityRemap redo(Registry& registry) override {
        EntityRemap combined;
        for (auto& child : children) {
            merge(combined, child->redo(registry));
        }
        return combined;
    }
    void remap(const EntityRemap& mapping) override {
        for (auto& child : children) {
            child->remap(mapping);
        }
    }

private:
    static void merge(EntityRemap& into, const EntityRemap& from) {
        for (const auto& [oldId, newId] : from) {
            into[oldId] = newId;
        }
    }
    std::vector<std::unique_ptr<EditorCommand>> children;
};

// Transform edit from the gizmo or the inspector. Entity persists across the
// edit; remap keeps the reference valid if the entity is later recreated, and
// tryMerge coalesces back-to-back edits of the same entity into one step.
class TransformCommand : public EditorCommand {
public:
    TransformCommand(Entity entity, const Transform& before, const Transform& after)
        : entity(entity), before(before), after(after) {}

    EntityRemap undo(Registry& registry) override { apply(registry, before); return {}; }
    EntityRemap redo(Registry& registry) override { apply(registry, after); return {}; }

    void remap(const EntityRemap& mapping) override { entity = remapped(mapping, entity); }

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

    EntityRemap undo(Registry& registry) override { trySetParent(registry, oldParent); return {}; }
    EntityRemap redo(Registry& registry) override { trySetParent(registry, newParent); return {}; }

    void remap(const EntityRemap& mapping) override {
        child = remapped(mapping, child);
        oldParent = remapped(mapping, oldParent);
        newParent = remapped(mapping, newParent);
    }

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
// re-instantiates through the same path as prefabs. `currentEntities` tracks the
// subtree's ids (object order, root first); on each recreate it yields an
// old->new remap so other commands referencing this subtree stay valid.
class CreateSubtreeCommand : public EditorCommand {
public:
    CreateSubtreeCommand(std::string serialized, Entity parent, std::vector<Entity> created)
        : serialized(std::move(serialized)), parent(parent), currentEntities(std::move(created)) {}

    EntityRemap undo(Registry& registry) override {
        destroyEntitySubtree(registry, root());
        return {}; // ids kept in currentEntities as the "old" side for the next redo
    }

    EntityRemap redo(Registry& registry) override {
        std::vector<Entity> created;
        const Entity newRoot = SceneSerializer::instantiateFromString(registry, serialized, &created);
        if (newRoot != INVALID_ENTITY && parent != INVALID_ENTITY && registry.transforms.has(parent)) {
            try {
                registry.setParent(newRoot, parent);
            } catch (...) {
            }
        }
        EntityRemap mapping = buildRemap(currentEntities, created);
        currentEntities = std::move(created);
        return mapping;
    }

    void remap(const EntityRemap& mapping) override {
        parent = remapped(mapping, parent);
        for (Entity& entity : currentEntities) {
            entity = remapped(mapping, entity);
        }
    }

private:
    Entity root() const { return currentEntities.empty() ? INVALID_ENTITY : currentEntities.front(); }

    std::string serialized;
    Entity parent;
    std::vector<Entity> currentEntities;
};

// Deletion of an entity subtree — the mirror of CreateSubtreeCommand. The caller
// has already destroyed the subtree (after recording its serialized form and id
// order), so undo re-instantiates + remaps, and redo destroys again.
class DeleteSubtreeCommand : public EditorCommand {
public:
    DeleteSubtreeCommand(std::string serialized, Entity parent, std::vector<Entity> deletedOrder)
        : serialized(std::move(serialized)), parent(parent), currentEntities(std::move(deletedOrder)) {}

    EntityRemap undo(Registry& registry) override {
        std::vector<Entity> created;
        const Entity newRoot = SceneSerializer::instantiateFromString(registry, serialized, &created);
        if (newRoot != INVALID_ENTITY && parent != INVALID_ENTITY && registry.transforms.has(parent)) {
            try {
                registry.setParent(newRoot, parent);
            } catch (...) {
            }
        }
        EntityRemap mapping = buildRemap(currentEntities, created);
        currentEntities = std::move(created);
        return mapping;
    }

    EntityRemap redo(Registry& registry) override {
        destroyEntitySubtree(registry, root());
        return {};
    }

    void remap(const EntityRemap& mapping) override {
        parent = remapped(mapping, parent);
        for (Entity& entity : currentEntities) {
            entity = remapped(mapping, entity);
        }
    }

private:
    Entity root() const { return currentEntities.empty() ? INVALID_ENTITY : currentEntities.front(); }

    std::string serialized;
    Entity parent;
    std::vector<Entity> currentEntities;
};

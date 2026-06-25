#pragma once

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
// adding/removing a value-only component) where a dedicated class is overkill.
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

// Groups several commands into one undo/redo step (multi-select duplicate/delete).
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

// Transform edit from the gizmo or the inspector. The entity persists across the
// edit, so its id is stable and we can store before/after transforms directly.
class TransformCommand : public EditorCommand {
public:
    TransformCommand(Entity entity, const Transform& before, const Transform& after)
        : entity(entity), before(before), after(after) {}

    void undo(Registry& registry) override { apply(registry, before); }
    void redo(Registry& registry) override { apply(registry, after); }

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

// Hierarchy reparent from drag-and-drop. Entity persists; we just remember the
// old and new parent. setParent rejects cycles, so guard against throws.
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
            // A reparent that would now form a cycle is simply skipped.
        }
    }

    Entity child;
    Entity oldParent;
    Entity newParent;
};

// Creation of an entity subtree (duplicate). The subtree is stored serialized, so
// redo re-instantiates it through the same path as prefabs (correct component +
// resource-refcount handling). Because ids are reassigned on each re-instantiate,
// the current root is tracked so undo can destroy the right subtree.
//
// Limitation: a TransformCommand recorded against a duplicated entity will not
// survive "undo past the duplicate, then redo" (the entity's id changes). This is
// the known id-stability edge case of a command-based history; documented in the
// ROADMAP under Phase 10B.
class CreateSubtreeCommand : public EditorCommand {
public:
    CreateSubtreeCommand(std::string serialized, Entity parent, Entity currentRoot)
        : serialized(std::move(serialized)), parent(parent), currentRoot(currentRoot) {}

    void undo(Registry& registry) override {
        destroyEntitySubtree(registry, currentRoot);
        currentRoot = INVALID_ENTITY;
    }

    void redo(Registry& registry) override {
        currentRoot = SceneSerializer::instantiateFromString(registry, serialized);
        if (currentRoot != INVALID_ENTITY && parent != INVALID_ENTITY && registry.transforms.has(parent)) {
            try {
                registry.setParent(currentRoot, parent);
            } catch (...) {
            }
        }
    }

private:
    std::string serialized;
    Entity parent;
    Entity currentRoot;
};

// Deletion of an entity subtree. The mirror of CreateSubtreeCommand: the caller
// has already destroyed the subtree (after capturing its serialized form), so
// undo re-instantiates it and redo destroys it again. Same id-reassignment
// caveat as CreateSubtreeCommand.
class DeleteSubtreeCommand : public EditorCommand {
public:
    DeleteSubtreeCommand(std::string serialized, Entity parent)
        : serialized(std::move(serialized)), parent(parent), currentRoot(INVALID_ENTITY) {}

    void undo(Registry& registry) override {
        currentRoot = SceneSerializer::instantiateFromString(registry, serialized);
        if (currentRoot != INVALID_ENTITY && parent != INVALID_ENTITY && registry.transforms.has(parent)) {
            try {
                registry.setParent(currentRoot, parent);
            } catch (...) {
            }
        }
    }

    void redo(Registry& registry) override {
        destroyEntitySubtree(registry, currentRoot);
        currentRoot = INVALID_ENTITY;
    }

private:
    std::string serialized;
    Entity parent;
    Entity currentRoot;
};

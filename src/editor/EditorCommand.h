#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "ecs/Entity.h"

class Registry;

// Maps old entity ids to new ones after a subtree is destroyed and recreated
// (duplicate/delete undo, prefab respawn). Returned by undo()/redo() so the
// history can patch every *other* command that still references the old ids.
using EntityRemap = std::unordered_map<Entity, Entity>;

// Base for an editor action that can be undone and redone. Commands record an
// *already-applied* change (the editor mutates the registry, then pushes a
// command describing how to reverse/replay it). undo()/redo() return an
// EntityRemap when they reassign entity ids (usually empty).
class EditorCommand {
public:
    virtual ~EditorCommand() = default;

    virtual EntityRemap undo(Registry& registry) = 0;
    virtual EntityRemap redo(Registry& registry) = 0;

    // Rewrite any entity ids this command stores, given an old->new mapping.
    virtual void remap(const EntityRemap& mapping) { (void)mapping; }

    // Compression: if this command can absorb `next` (same target/kind, applied
    // back-to-back), fold `next` into this one and return true. Default: no.
    virtual bool tryMerge(const EditorCommand& next) { (void)next; return false; }
};

// Linear undo/redo stack with transactions, command-id stamping, entity
// remapping, and adjacent-command compression. Cleared when the scene is
// replaced wholesale (remapping handles *within-session* recreation; a full
// scene reload is a different identity space).
class CommandHistory {
public:
    // --- transactions -------------------------------------------------------
    // Between begin and commit, pushed commands accumulate into one atomic entry.
    // commit() stores it as a single undo step; abort() rolls it back and drops it.
    void beginTransaction() { ++transactionDepth; }

    void commitTransaction(Registry& registry) {
        if (transactionDepth == 0) {
            return;
        }
        if (--transactionDepth == 0 && pending) {
            if (!pending->empty()) {
                storeEntry(std::move(pending));
            }
            pending.reset();
        }
        (void)registry;
    }

    void abortTransaction(Registry& registry) {
        if (transactionDepth == 0) {
            return;
        }
        transactionDepth = 0;
        if (pending) {
            pending->undo(registry); // revert what was applied so far
            pending.reset();
        }
    }

    // --- pushing ------------------------------------------------------------
    void push(std::unique_ptr<EditorCommand> command) {
        if (transactionDepth > 0) {
            if (!pending) {
                pending = std::make_unique<TransactionEntry>();
            }
            pending->add(std::move(command));
            return;
        }
        // Compression: let the most recent entry absorb a same-target command.
        if (index > 0 && index == entries.size() && entries[index - 1].command->tryMerge(*command)) {
            return;
        }
        storeEntry(std::move(command));
    }

    // --- navigation ---------------------------------------------------------
    bool canUndo() const { return index > 0; }
    bool canRedo() const { return index < entries.size(); }

    void undo(Registry& registry) {
        if (!canUndo()) {
            return;
        }
        --index;
        applyRemap(entries[index].command->undo(registry), index);
    }

    void redo(Registry& registry) {
        if (!canRedo()) {
            return;
        }
        applyRemap(entries[index].command->redo(registry), index);
        ++index;
    }

    void clear() {
        entries.clear();
        index = 0;
        pending.reset();
        transactionDepth = 0;
    }

    size_t size() const { return entries.size(); }

private:
    // One CompositeCommand-like accumulator used while a transaction is open.
    class TransactionEntry : public EditorCommand {
    public:
        void add(std::unique_ptr<EditorCommand> command) { children.push_back(std::move(command)); }
        bool empty() const { return children.empty(); }

        EntityRemap undo(Registry& registry) override {
            EntityRemap combined;
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                mergeRemap(combined, (*it)->undo(registry));
            }
            return combined;
        }
        EntityRemap redo(Registry& registry) override {
            EntityRemap combined;
            for (auto& child : children) {
                mergeRemap(combined, child->redo(registry));
            }
            return combined;
        }
        void remap(const EntityRemap& mapping) override {
            for (auto& child : children) {
                child->remap(mapping);
            }
        }

    private:
        static void mergeRemap(EntityRemap& into, const EntityRemap& from) {
            for (const auto& [oldId, newId] : from) {
                into[oldId] = newId;
            }
        }
        std::vector<std::unique_ptr<EditorCommand>> children;
    };

    struct Entry {
        std::unique_ptr<EditorCommand> command;
        uint64_t id = 0; // stable per-session command id (debug/introspection)
    };

    void storeEntry(std::unique_ptr<EditorCommand> command) {
        entries.resize(index); // discard any redoable tail
        entries.push_back(Entry{std::move(command), ++nextCommandId});
        index = entries.size();
    }

    // Patch every command except the one at `sourceIndex` with the id remap a
    // recreate produced, so references stay valid across destroy/recreate.
    void applyRemap(const EntityRemap& mapping, size_t sourceIndex) {
        if (mapping.empty()) {
            return;
        }
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i != sourceIndex) {
                entries[i].command->remap(mapping);
            }
        }
    }

    std::vector<Entry> entries;
    size_t index = 0;                 // applied count; [index, end) is the redo tail
    uint64_t nextCommandId = 0;
    int transactionDepth = 0;
    std::unique_ptr<TransactionEntry> pending;
};

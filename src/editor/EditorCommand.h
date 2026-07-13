#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "ecs/Entity.h"

class Registry;

// Base for an editor action that can be undone and redone. Commands record an
// *already-applied* change (the editor mutates the registry, then pushes a
// command describing how to reverse/replay it).
//
// Since Phase 14B, a destroyed subtree is recreated into its *original* entity
// ids (see SceneSerializer::instantiateFromStringWithIds), so any raw ids a
// command stores stay valid across a destroy/recreate — no id-remap layer needed.
class EditorCommand {
public:
    virtual ~EditorCommand() = default;

    virtual void undo(Registry& registry) = 0;
    virtual void redo(Registry& registry) = 0;

    // Compression: if this command can absorb `next` (same target/kind, applied
    // back-to-back), fold `next` into this one and return true. Default: no.
    virtual bool tryMerge(const EditorCommand& next) { (void)next; return false; }
};

// Linear undo/redo stack with transactions, command-id stamping, and
// adjacent-command compression. Cleared when the scene is replaced wholesale
// (a full scene reload is a different identity space).
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
        entries[index].command->undo(registry);
    }

    void redo(Registry& registry) {
        if (!canRedo()) {
            return;
        }
        entries[index].command->redo(registry);
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
    // Children undo in reverse of the order they were applied.
    class TransactionEntry : public EditorCommand {
    public:
        void add(std::unique_ptr<EditorCommand> command) { children.push_back(std::move(command)); }
        bool empty() const { return children.empty(); }

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

    struct Entry {
        std::unique_ptr<EditorCommand> command;
        uint64_t id = 0; // stable per-session command id (debug/introspection)
    };

    void storeEntry(std::unique_ptr<EditorCommand> command) {
        entries.resize(index); // discard any redoable tail
        entries.push_back(Entry{std::move(command), ++nextCommandId});
        index = entries.size();
    }

    std::vector<Entry> entries;
    size_t index = 0;                 // applied count; [index, end) is the redo tail
    uint64_t nextCommandId = 0;
    int transactionDepth = 0;
    std::unique_ptr<TransactionEntry> pending;
};

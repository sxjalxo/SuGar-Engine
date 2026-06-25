#pragma once

#include <memory>
#include <vector>

class Registry;

// Base for an editor action that can be undone and redone. Commands record an
// *already-applied* change (the editor mutates the registry, then pushes a
// command describing how to reverse/replay it). All mutation goes through the
// Registry, in keeping with "all state is serializable component state".
class EditorCommand {
public:
    virtual ~EditorCommand() = default;
    virtual void undo(Registry& registry) = 0;
    virtual void redo(Registry& registry) = 0;
};

// A linear undo/redo stack. push() stores an already-applied command and drops
// any redo tail; undo()/redo() walk the stack. Must be cleared whenever the
// scene is replaced (Stop / load), since entity ids then become invalid.
class CommandHistory {
public:
    void push(std::unique_ptr<EditorCommand> command) {
        commands.resize(index);              // discard any redoable tail
        commands.push_back(std::move(command));
        index = commands.size();
    }

    bool canUndo() const { return index > 0; }
    bool canRedo() const { return index < commands.size(); }

    void undo(Registry& registry) {
        if (!canUndo()) {
            return;
        }
        --index;
        commands[index]->undo(registry);
    }

    void redo(Registry& registry) {
        if (!canRedo()) {
            return;
        }
        commands[index]->redo(registry);
        ++index;
    }

    void clear() {
        commands.clear();
        index = 0;
    }

private:
    std::vector<std::unique_ptr<EditorCommand>> commands;
    size_t index = 0; // number of applied commands; [index, end) is the redo tail
};

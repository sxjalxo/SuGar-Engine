#pragma once

#include <filesystem>
#include <string>

// Loads the hot-swappable game module DLL and invokes its registerGameBehaviors()
// entry point. To allow recompiling the DLL while the engine runs (Windows locks
// a loaded DLL), it loads a *copy* of the module and leaves the original free for
// the build to overwrite. The Windows loading API is confined to the .cpp so
// <windows.h> doesn't leak; cross-platform dlopen slots in later.
class GameModuleLoader {
public:
    ~GameModuleLoader();

    // Loads module `moduleName` (e.g. "SuGarGame"): copies <dir>/<name>.dll to a live
    // copy, loads that, resolves + calls registerGameBehaviors. Returns false (and logs)
    // if the DLL or symbol is missing — the engine keeps running.
    //
    // `directory` empty = the executable's own directory (the built-in module). An M4
    // game passes its own directory so its behaviours DLL loads from the game folder,
    // outside the engine repo; SuGarCore.dll still resolves from the exe dir, which is
    // always on the Windows loader search path.
    bool load(std::string moduleName, const std::filesystem::path& directory = {});

    // Re-copies + reloads the same module, picking up a recompiled DLL. Safe
    // because unload clears the registry (destroying DLL-owned behavior instances)
    // before FreeLibrary, and behaviors reconnect by name afterwards.
    bool reload();

    void unload();

    bool isLoaded() const { return handle != nullptr; }

    // True if the source DLL has been rebuilt (newer timestamp) since it was
    // loaded — the signal the main loop uses to auto hot-reload.
    bool sourceChanged() const;

private:
    std::filesystem::path exeDirectory() const;
    std::filesystem::path moduleDirectory() const; // where the DLL + its live copies live
    std::filesystem::path sourcePath() const;
    void removeStaleLiveCopies() const; // best-effort cleanup of prior copies

    void* handle = nullptr; // HMODULE, opaque here to keep <windows.h> out
    std::string moduleName;
    std::filesystem::path moduleDir; // empty = exe dir; a game passes its own folder
    std::filesystem::path currentLive;   // the uniquely-named copy we loaded
    std::filesystem::file_time_type loadedWriteTime{};        // source mtime at last successful load
    mutable std::filesystem::file_time_type lastSeenWriteTime{}; // for debouncing the watch
    unsigned liveCounter = 0;            // makes each live copy a fresh filename
};

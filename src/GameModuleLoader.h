#pragma once

#include <string>

// Loads the game module DLL and invokes its registerGameBehaviors() entry point.
// The Windows loading API is confined to the .cpp so <windows.h> doesn't leak
// into the rest of the engine. Cross-platform dlopen support slots in later.
class GameModuleLoader {
public:
    ~GameModuleLoader();

    // Loads `dllName`, resolves + calls registerGameBehaviors(). Returns false
    // (and logs) if the DLL or symbol is missing — the engine keeps running,
    // just without game behaviors. Reloading first clears the registry + unloads.
    bool load(const std::string& dllName);
    void unload();

    bool isLoaded() const { return handle != nullptr; }

private:
    void* handle = nullptr; // HMODULE, opaque here to keep <windows.h> out
    std::string loadedName;
};

#include "GameModuleLoader.h"

#include <iostream>

#include "game/GameModule.h"
#include "scene/BehaviorRegistry.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

GameModuleLoader::~GameModuleLoader() {
    unload();
}

bool GameModuleLoader::load(const std::string& dllName) {
    unload();

    HMODULE module = LoadLibraryA(dllName.c_str());
    if (module == nullptr) {
        std::cerr << "[GameModule] failed to load " << dllName << "\n";
        return false;
    }

    using RegisterFn = void (*)();
    auto registerGameBehaviors =
        reinterpret_cast<RegisterFn>(GetProcAddress(module, kRegisterGameBehaviorsSymbol));
    if (registerGameBehaviors == nullptr) {
        std::cerr << "[GameModule] '" << kRegisterGameBehaviorsSymbol << "' not found in " << dllName << "\n";
        FreeLibrary(module);
        return false;
    }

    handle = module;
    loadedName = dllName;
    registerGameBehaviors();
    std::cout << "[GameModule] loaded " << dllName << " and registered game behaviors\n";
    return true;
}

void GameModuleLoader::unload() {
    if (handle == nullptr) {
        return;
    }
    // Destroy behavior instances (they live in the DLL's memory) BEFORE freeing
    // the DLL, or their vtables would dangle when the registry is later cleared.
    BehaviorRegistry::clear();
    FreeLibrary(static_cast<HMODULE>(handle));
    handle = nullptr;
    loadedName.clear();
}

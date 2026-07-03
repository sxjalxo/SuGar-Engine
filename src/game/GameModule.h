#pragma once

// Contract between the engine (which loads the module) and the game module DLL
// (which implements it). The game module compiles into its own DLL that links
// ONLY against Core; the engine resolves the entry point below at runtime and
// calls it to register the game's behaviors into Core's BehaviorRegistry.

#if defined(_WIN32)
  #define SUGAR_GAME_EXPORT extern "C" __declspec(dllexport)
#else
  #define SUGAR_GAME_EXPORT extern "C" __attribute__((visibility("default")))
#endif

// Symbol the engine's GameModuleLoader resolves via GetProcAddress/dlsym.
// Signature: void registerGameBehaviors();
inline constexpr const char* kRegisterGameBehaviorsSymbol = "registerGameBehaviors";

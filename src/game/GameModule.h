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

// --- Core ABI version -------------------------------------------------------
//
// A game module compiles against Core's headers and then loads next to a Core built
// from possibly different sources. Most mismatches announce themselves: a removed
// symbol fails to resolve, a resized struct corrupts memory loudly. Generational
// entity ids introduced a mismatch that does neither — `Entity` kept its size and
// its name, and only the MEANING of its bits changed, so a stale Game.dll links,
// loads, runs, and quietly misinterprets every handle it touches
// (DevDocs/DESIGN_GENERATIONAL_IDS.md).
//
// So the module states which Core contract it was built against, and the loader
// refuses anything else. A DLL built before this existed exports no version symbol
// at all and is refused on that basis — which is the correct answer for it too.
//
// Bump this whenever a Core type a game DLL compiles against changes meaning,
// layout, or size.
inline constexpr unsigned int kSuGarCoreABIVersion = 2; // 2: packed generational Entity

// Symbol the engine resolves alongside the entry point. Signature: unsigned int().
inline constexpr const char* kCoreABIVersionSymbol = "sugarCoreABIVersion";

// Every game module must expand this exactly once, next to its
// registerGameBehaviors() definition. It is a macro rather than an inline function in
// this header because the header is included by the engine executable too, and the
// engine must not export game-module symbols (Rule 15 layering).
#define SUGAR_DECLARE_GAME_MODULE_ABI()                                    \
    SUGAR_GAME_EXPORT unsigned int sugarCoreABIVersion() {                 \
        return kSuGarCoreABIVersion;                                       \
    }

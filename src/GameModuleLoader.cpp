#include "GameModuleLoader.h"

#include <chrono>
#include <iostream>
#include <system_error>
#include <thread>

#include "game/GameModule.h"
#include "scene/BehaviorRegistry.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace fs = std::filesystem;

GameModuleLoader::~GameModuleLoader() {
    unload();
}

fs::path GameModuleLoader::exeDirectory() const {
    char buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return fs::current_path();
    }
    return fs::path(buffer).parent_path();
}

fs::path GameModuleLoader::moduleDirectory() const {
    return moduleDir.empty() ? exeDirectory() : moduleDir;
}

fs::path GameModuleLoader::sourcePath() const {
    return moduleDirectory() / (moduleName + ".dll");
}

// Removes leftover <moduleName>_live_*.dll from prior loads/runs (best-effort;
// a copy that's still briefly locked after FreeLibrary is simply skipped).
void GameModuleLoader::removeStaleLiveCopies() const {
    if (moduleName.empty()) {
        return;
    }
    const std::string prefix = moduleName + "_live_";
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(moduleDirectory(), ec)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0 && entry.path() != currentLive) {
            std::error_code removeEc;
            fs::remove(entry.path(), removeEc);
        }
    }
}

bool GameModuleLoader::load(std::string name, const fs::path& directory) {
    unload();
    moduleName = std::move(name);
    moduleDir = directory;
    removeStaleLiveCopies();

    const fs::path source = sourcePath();
    std::error_code ec;
    if (!fs::exists(source, ec)) {
        std::cerr << "[GameModule] not found: " << source.string() << "\n";
        return false;
    }
    const auto newWriteTime = fs::last_write_time(source, ec);

    // Load a uniquely-named copy so the build can overwrite the original, and so
    // we never copy onto a file the OS is still releasing after FreeLibrary. Retry
    // the copy a few times to ride out the brief window where the just-finished
    // build still holds the source (mirrors the asset hot-reload retry policy).
    const fs::path live = moduleDirectory() / (moduleName + "_live_" + std::to_string(liveCounter++) + ".dll");
    bool copied = false;
    for (int attempt = 0; attempt < 10; ++attempt) {
        ec.clear();
        fs::copy_file(source, live, fs::copy_options::overwrite_existing, ec);
        if (!ec) {
            copied = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!copied) {
        std::cerr << "[GameModule] failed to copy to live DLL: " << ec.message() << "\n";
        return false; // loadedWriteTime unchanged -> the watch retries when stable
    }

    // LoadLibraryW with the path's native wide form: LoadLibraryA can't represent a
    // module directory containing non-ASCII characters (a user profile like
    // C:\Users\José\...) under a non-UTF-8 ANSI codepage, and would fail to load a
    // perfectly valid DLL. fs::path::wstring() is already the OS-native encoding.
    HMODULE module = LoadLibraryW(live.wstring().c_str());
    if (module == nullptr) {
        std::cerr << "[GameModule] failed to load " << live.string() << "\n";
        return false;
    }
    currentLive = live;

    // Refuse a module built against a different Core contract BEFORE calling anything
    // in it. The mismatch this exists for is silent: generational entity ids changed
    // what an Entity's bits mean without changing its size, so a stale DLL would load
    // cleanly and then misread every handle it was given. Checked before the entry
    // point is even resolved, so a rejected module never runs a line of its code.
    // A module that loaded but is definitively WRONG (missing or mismatched symbols)
    // is not a transient failure: the same bytes will be just as wrong next frame. Bank
    // its timestamp so the hot-reload watch waits for a genuine rebuild instead of
    // retrying — without this an unrebuilt game DLL is rejected every single frame,
    // measured at 204 attempts in 10 seconds. A copy/IO failure keeps the old behaviour
    // (timestamp untouched, so it is retried) because that one really can succeed later.
    auto rejectModule = [&](HMODULE rejected) {
        FreeLibrary(rejected);
        loadedWriteTime = newWriteTime;
        lastSeenWriteTime = newWriteTime;
        return false;
    };

    using AbiVersionFn = unsigned int (*)();
    auto coreABIVersion =
        reinterpret_cast<AbiVersionFn>(GetProcAddress(module, kCoreABIVersionSymbol));
    if (coreABIVersion == nullptr) {
        std::cerr << "[GameModule] " << moduleName << " exports no '" << kCoreABIVersionSymbol
                  << "': built against a Core older than the ABI stamp. Rebuild the game module.\n";
        return rejectModule(module);
    }
    if (const unsigned int moduleABI = coreABIVersion(); moduleABI != kSuGarCoreABIVersion) {
        std::cerr << "[GameModule] " << moduleName << " was built against Core ABI " << moduleABI
                  << ", engine is " << kSuGarCoreABIVersion << ". Rebuild the game module.\n";
        return rejectModule(module);
    }

    using RegisterFn = void (*)();
    auto registerGameBehaviors =
        reinterpret_cast<RegisterFn>(GetProcAddress(module, kRegisterGameBehaviorsSymbol));
    if (registerGameBehaviors == nullptr) {
        std::cerr << "[GameModule] '" << kRegisterGameBehaviorsSymbol << "' not found\n";
        return rejectModule(module);
    }

    handle = module;
    loadedWriteTime = newWriteTime; // only advance on a successful load, so a failed
    lastSeenWriteTime = newWriteTime; // reload keeps being retried by the watch
    registerGameBehaviors();
    std::cout << "[GameModule] loaded " << moduleName << " and registered game behaviors\n";
    return true;
}

bool GameModuleLoader::reload() {
    if (moduleName.empty()) {
        return false;
    }
    std::cout << "[GameModule] reloading " << moduleName << "...\n";
    return load(moduleName, moduleDir);
}

bool GameModuleLoader::sourceChanged() const {
    if (moduleName.empty()) {
        return false; // nothing ever loaded; not gated on handle so failed reloads recover
    }
    std::error_code ec;
    const auto now = fs::last_write_time(sourcePath(), ec);
    if (ec) {
        return false; // e.g. mid-rebuild the file is momentarily inaccessible
    }
    if (now == loadedWriteTime) {
        return false; // unchanged since the last successful load
    }
    // Debounce: only act once the timestamp has settled (same across two polls),
    // i.e. the build has finished writing the DLL.
    const bool settled = (now == lastSeenWriteTime);
    lastSeenWriteTime = now;
    return settled;
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

    // Best-effort delete of the copy we just freed (may still be briefly locked;
    // if so it's swept up by removeStaleLiveCopies on the next load).
    if (!currentLive.empty()) {
        std::error_code ec;
        fs::remove(currentLive, ec);
        currentLive.clear();
    }
}

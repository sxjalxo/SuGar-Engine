#include "assets/Packager.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "assets/AssetCooker.h"
#include "assets/AssetDatabase.h"
#include "assets/AssetManifest.h"
#include "assets/AssetMeta.h"
#include "assets/AssetPath.h"
#include "scene/SceneSerializer.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::string readFile(const std::string& path, bool& ok) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ok = false;
        return std::string();
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    ok = true;
    return buffer.str();
}

std::string lowerExtension(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    for (char& character : extension) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte >= 'A' && byte <= 'Z') {
            character = static_cast<char>(byte - 'A' + 'a');
        }
    }
    return extension;
}

bool isAllDigits(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    return std::all_of(value.begin(), value.end(),
                       [](unsigned char c) { return c >= '0' && c <= '9'; });
}

bool copyInto(const std::string& sourcePath, const std::filesystem::path& destPath,
              std::vector<std::string>& errors) {
    std::error_code directoryError;
    std::filesystem::create_directories(destPath.parent_path(), directoryError);

    std::error_code copyError;
    std::filesystem::copy_file(sourcePath, destPath,
                               std::filesystem::copy_options::overwrite_existing, copyError);
    if (copyError) {
        errors.push_back("could not copy '" + sourcePath + "' -> '" +
                         destPath.generic_string() + "': " + copyError.message());
        return false;
    }
    return true;
}

} // namespace

std::string Packager::executablePath() {
#ifdef _WIN32
    char buffer[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return std::string();
    }
    return std::filesystem::path(buffer).generic_string();
#else
    // Non-Windows is not a shipping target yet (Rule 18). The build pipeline runs on
    // the dev platform; a portable exe-path lookup lands when another OS does.
    return std::string();
#endif
}

std::vector<std::string> Packager::collectRuntimeBinaries() {
    std::vector<std::string> binaries;
    const std::string exe = executablePath();
    if (exe.empty()) {
        return binaries;
    }
    binaries.push_back(exe);

    // Every DLL beside the executable, except the hot-reload live copies (the loader
    // makes SuGarGame_live_*.dll each reload; those are transient, gitignored, and must
    // never ship). Enumerated rather than hard-coded so a new engine DLL is picked up
    // without editing packaging.
    std::error_code error;
    const std::filesystem::path exeDir = std::filesystem::path(exe).parent_path();
    for (const auto& entry : std::filesystem::directory_iterator(exeDir, error)) {
        if (error || !entry.is_regular_file()) {
            continue;
        }
        const std::string name = entry.path().filename().string();
        if (entry.path().extension() == ".dll" && name.find("_live_") == std::string::npos) {
            binaries.push_back(entry.path().generic_string());
        }
    }
    return binaries;
}

bool Packager::verify(const std::string& packageRoot, std::vector<std::string>& errors) {
    AssetManifest manifest;
    std::string manifestError;
    if (!manifest.load(manifestPath(packageRoot), manifestError)) {
        errors.push_back(manifestError);
        return false;
    }

    // Resolve exactly as a shipped runtime does: packaged mode, no database, no source.
    // Save and restore the cooker's globals -- verify may run in-process right after a
    // package, and the editor must go back to source resolution afterward.
    const std::string previousCache = AssetCooker::cacheDirectory();
    AssetCooker::setManifest(&manifest);
    AssetCooker::setCacheDirectory(cacheDirectory(packageRoot));
    AssetCooker::clearMemo();

    const size_t before = errors.size();
    for (const auto& entry : manifest.all()) {
        std::string resolveError;
        // ensureCooked in packaged mode returns "" both for a key the manifest omits
        // and for one whose artifact file is missing (a broken package), so a non-empty
        // result already means "resolved to a real file on disk".
        const std::string artifact = AssetCooker::ensureCooked(entry.first, resolveError);
        if (artifact.empty()) {
            errors.push_back("verify: " + resolveError);
        }
    }

    AssetCooker::setManifest(nullptr);
    AssetCooker::setCacheDirectory(previousCache);
    AssetCooker::clearMemo();
    return errors.size() == before;
}

std::string Packager::manifestPath(const std::string& packageRoot) {
    return packageRoot + "/assets.manifest";
}

std::string Packager::cacheDirectory(const std::string& packageRoot) {
    return packageRoot + "/assetcache";
}

Packager::Report Packager::package(AssetDatabase& database, const Spec& spec) {
    Report report;
    const std::filesystem::path outRoot(spec.outputDirectory);

    // Cook directly into the package cache, so an artifact lands where it ships instead
    // of being cooked to the dev cache and copied. Save and restore the cooker's global
    // state: packaging is one operation, not a mode the editor stays in.
    const std::string previousCache = AssetCooker::cacheDirectory();
    AssetCooker::setDatabase(&database);
    AssetCooker::setCacheDirectory(cacheDirectory(spec.outputDirectory));
    AssetCooker::clearMemo();

    AssetManifest manifest;

    // --- reachability: scenes/prefabs -> asset keys ---------------------------
    // A prefab is a scene fragment, so it is both a file to ship and a source of more
    // keys. The worklist holds files still to parse; `processedFiles` stops a prefab
    // cycle from looping.
    std::vector<std::string> fileWorklist = spec.scenes;
    std::set<std::string> processedFiles;
    std::set<std::string> assetKeys(spec.extraAssetKeys.begin(), spec.extraAssetKeys.end());

    while (!fileWorklist.empty()) {
        const std::string filePath = fileWorklist.back();
        fileWorklist.pop_back();
        if (!processedFiles.insert(filePath).second) {
            continue;
        }

        bool readOk = false;
        const std::string text = readFile(filePath, readOk);
        if (!readOk) {
            report.errors.push_back("could not read scene/prefab: " + filePath);
            continue;
        }

        std::vector<std::string> keys;
        if (!SceneSerializer::collectAssetKeys(text, keys)) {
            report.errors.push_back("could not parse scene/prefab: " + filePath);
            continue;
        }

        // Ship the scene/prefab file itself, at the path the runtime will load it from.
        // An absolute source (an external game's scene) lands flat at the package root by
        // filename; a project-relative one keeps its relative path.
        const std::filesystem::path sceneSource(filePath);
        const std::filesystem::path sceneDest =
            sceneSource.is_absolute() ? (outRoot / sceneSource.filename()) : (outRoot / sceneSource);
        if (copyInto(filePath, sceneDest, report.errors)) {
            report.scenesPackaged++;
        }

        for (const std::string& key : keys) {
            // A prefab reference pulls in another file to parse and ship, not an asset to
            // cook.
            if (lowerExtension(key) == ".prefab") {
                fileWorklist.push_back(key);
            } else {
                assetKeys.insert(key);
            }
        }
    }

    // --- dependency closure ---------------------------------------------------
    // A model reaches its base-colour textures even when no scene names the texture
    // directly (Phase 19C edges). Discover first, then expand: the cooker must have
    // parsed the models for the database to know their edges.
    for (const std::string& key : assetKeys) {
        AssetCooker::discoverDependencies(key);
    }
    std::set<std::string> withDependencies = assetKeys;
    for (const std::string& key : assetKeys) {
        for (const std::string& dependency : database.dependenciesOf(key)) {
            withDependencies.insert(dependency);
        }
    }

    // --- cook + copy each reachable asset ------------------------------------
    std::set<std::string> copiedSourceModels;
    for (const std::string& key : withDependencies) {
        // Built-ins (builtin://checkerboard) have no source file and are created
        // procedurally by the runtime, so there is nothing to cook or ship -- skip them
        // silently rather than reporting a "source-backed" gap that isn't one.
        if (key.rfind("builtin://", 0) == 0) {
            continue;
        }

        const std::string pathPart = AssetPath::pathOf(key);
        if (pathPart.empty()) {
            report.unpackagedKeys.push_back(key);
            continue;
        }

        const std::string sub = AssetPath::subOf(key);
        const std::string extension = lowerExtension(pathPart);
        const AssetType type = assetTypeFromExtension(extension);

        // A named sub-key of a model (#Idle, #Humanoid) is an animation clip or a skin.
        // Those are not cooked artifacts (Phase 19 scoped them out); they reconstitute
        // at load by parsing the source model. Interim (DevDocs/DESIGN_PACKAGING.md): ship
        // the source model and report the key, rather than cooking the whole model as a
        // bogus mesh under the clip's name. A numeric sub (#3) IS a mesh index and cooks
        // normally, so only non-numeric subs take this path.
        if (type == AssetType::Model && !sub.empty() && !isAllDigits(sub)) {
            const AssetEntry* entry = database.find(pathPart);
            const std::string sourcePath = entry != nullptr ? entry->path : pathPart;
            if (copiedSourceModels.insert(pathPart).second) {
                // Where the SHIPPED runtime will look: the key's own directory under the
                // package root, keeping the file's real name. Not `outRoot / sourcePath`
                // -- a catalogued path is ABSOLUTE whenever the content root is outside
                // the working directory, which is every external game (SUGAR_GAME), and
                // `path / absolute` discards the left operand. That made the destination
                // identical to the source: the copy failed as a self-copy and the package
                // shipped no model at all, so the standalone rendered in bind pose.
                const std::filesystem::path destination =
                    outRoot / std::filesystem::path(pathPart).parent_path() /
                    std::filesystem::path(sourcePath).filename();
                if (copyInto(sourcePath, destination, report.errors)) {
                    report.sourceModelsCopied++;
                }
            }
            report.unpackagedKeys.push_back(key);
            continue;
        }

        if (type != AssetType::Model && type != AssetType::Texture && type != AssetType::Audio) {
            // A referenced key with no cooker and no interim (an unknown extension).
            // Reported, never silently dropped.
            report.unpackagedKeys.push_back(key);
            continue;
        }

        std::string cookError;
        const std::string cooked = AssetCooker::ensureCooked(key, cookError);
        if (cooked.empty()) {
            report.errors.push_back(cookError);
            continue;
        }

        // The artifact is already inside the package cache (cooked there directly); the
        // manifest records the key -> hash so the shipped runtime can find it without
        // source.
        manifest.set(key, AssetCooker::artifactKey(key));
        report.assetsPackaged++;
    }

    // --- manifest + binaries --------------------------------------------------
    std::string manifestError;
    if (!manifest.write(manifestPath(spec.outputDirectory), manifestError)) {
        report.errors.push_back(manifestError);
    }

    for (const std::string& binary : spec.binaries) {
        // Binaries go flat beside the manifest: the OS loader wants the executable and
        // its DLLs in one directory.
        const std::filesystem::path dest = outRoot / std::filesystem::path(binary).filename();
        if (copyInto(binary, dest, report.errors)) {
            report.binariesCopied++;
        }
    }

    // Loose runtime files keep their intended relative layout (e.g. build/shaders/x.spv,
    // assets/fonts/x.ttf), because the runtime looks for them by those exact paths.
    for (const auto& [source, destRelative] : spec.extraFiles) {
        if (copyInto(source, outRoot / destRelative, report.errors)) {
            report.extraFilesCopied++;
        }
    }

    // Restore the cooker to editor mode: the package is on disk now, and the editor (if
    // this ran in-process) must go back to cooking into its own cache from source.
    AssetCooker::setCacheDirectory(previousCache);
    AssetCooker::clearMemo();

    return report;
}

#pragma once

#include <string>
#include <vector>

class AssetDatabase;

// Standalone export (docs/DESIGN_PACKAGING.md, Phase 20).
//
// A package is the cooked artifacts a set of scenes can reach, plus the manifest that
// lets a shipped runtime find them without the source tree, plus the scenes and the
// runtime binaries. The runtime resolves keys through the manifest (AssetCooker packaged
// mode) and parses no source format.
//
// Engine layer: it drives the cooker (which parses source formats) and copies files. It
// needs no Vulkan device -- cooking is device-free and copying is I/O -- so it runs
// headless under SUGAR_PACKAGE=1, the same shape as SUGAR_COOK.
//
// Orchestrates and owns nothing after it returns: the manifest belongs to the package on
// disk, reachability is answered by the database, cooking by the cooker.
class Packager {
public:
    struct Spec {
        // Scene / prefab files to ship. The roots of the reachability walk: everything
        // the package contains is reached from these.
        std::vector<std::string> scenes;

        // Extra asset keys to ship regardless of any scene (a menu texture a scene loads
        // by code, say). Optional -- most content is reached through the scenes.
        std::vector<std::string> extraAssetKeys;

        // Runtime binaries copied verbatim: the executable and its DLLs. Listed rather
        // than discovered, so packaging is testable and never guesses which files the
        // OS loader needs. The build pipeline (next phase) fills these in.
        std::vector<std::string> binaries;

        // Output directory. Overwritten in place; not cleaned first, so a stale artifact
        // from a previous package can linger -- harmless (the manifest names only current
        // ones) but a fresh directory is cleaner for a release.
        std::string outputDirectory;
    };

    struct Report {
        int scenesPackaged = 0;
        int assetsPackaged = 0;      // cooked artifacts copied
        int sourceModelsCopied = 0;  // interim: models shipped as source for clips/skins
        int binariesCopied = 0;

        // Referenced keys that could not be packaged as a cooked artifact. Reported, not
        // dropped (Rule 13): today these are #clip / #skin sub-keys with no cooker, and
        // seeing them is what will eventually justify cooking clips (Rule 18).
        std::vector<std::string> unpackagedKeys;

        // Hard failures (a scene that would not parse, an artifact that would not copy).
        // Non-empty means the package is incomplete.
        std::vector<std::string> errors;

        bool ok() const { return errors.empty(); }
    };

    // Builds the package described by `spec`, using `database` for reachability and
    // source locations. `database` must already have scanned the asset tree. Returns a
    // report; check `ok()`.
    static Report package(AssetDatabase& database, const Spec& spec);

    // Where a shipped runtime looks for its manifest and cache, relative to the working
    // directory (which for a package is the executable's own directory). Shared by the
    // packager (where it writes) and the runtime (where it reads), so the two never
    // disagree on the layout.
    static std::string manifestPath(const std::string& packageRoot);
    static std::string cacheDirectory(const std::string& packageRoot);
};

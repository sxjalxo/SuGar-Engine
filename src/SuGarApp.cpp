#include "SuGarApp.h"
#include "CrashHandler.h"
#include "Renderer.h"
#include "animation/AnimationStateSystem.h"
#include "animation/AnimationSystem.h"
#include "navigation/NavigationSystem.h"
#include "assets/AssetCooker.h"
#include "assets/AssetDatabase.h"
#include "assets/AssetManifest.h"
#include "assets/AssetReimport.h"
#include "assets/Packager.h"
#include "assets/AssetGateway.h"
#include "assets/ResourceManager.h"
#include "audio/AudioSystem.h"
#include "SelfTests.h"
#include "Benchmarks.h"
#include "StressTests.h"
#include "ui/RuntimeUIView.h"
#include "ui/RuntimeUISystem.h"
#include "core/EnginePaths.h"
#include "core/Input.h"
#include "core/InputActions.h"
#include "core/SaveData.h"
#include "rendering/Camera.h"
#include "rendering/Material.h"
#include "scene/Behavior.h"
#include "scene/BehaviorRegistry.h"
#include "scene/DrawList.h"
#include "scene/SceneSerializer.h"
#include "scene/ScriptSystem.h"
#include "scene/TransformMath.h"
#include "imgui.h"
#include "stb_image.h"
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <set>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <algorithm>
#include <chrono>
#include <thread>
#include <GLFW/glfw3.h>

const int WIDTH = 800;
const int HEIGHT = 600;
const int MAX_FRAMES_IN_FLIGHT = 2;

// Gameplay runs on a fixed 60 Hz step so simulation is deterministic and
// frame-rate independent; rendering remains uncapped.
constexpr float FIXED_TIMESTEP = 1.0f / 60.0f;
constexpr float MAX_ACCUMULATED_TIME = 0.25f;

const std::vector<const char*> validationLayers = {
    "VK_LAYER_KHRONOS_validation"
};

const std::vector<const char*> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

// --- Helper Functions ---

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

    return VK_FALSE;
}

VkResult CreateDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void DestroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

static std::string resolveAssetPath(const std::string& relativePath) {
    return EnginePaths::resolve(relativePath);
}

// The window icon (title bar, taskbar while running, Alt-Tab) is GLFW's, not the .exe
// resource's, so it is set from the same cube artwork at startup. Missing artwork is not
// an error: the window simply keeps the platform default.
void SuGarApp::setWindowIcon() {
    const std::string path = resolveAssetPath("assets/branding/sugar_cube.png");
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr) {
        std::cout << "[icon] no window icon at " << path << " (using the platform default)\n";
        return;
    }

    GLFWimage icon{};
    icon.width = width;
    icon.height = height;
    icon.pixels = pixels;
    glfwSetWindowIcon(window, 1, &icon);
    stbi_image_free(pixels);
}

static Entity findOrbitParentEntity(const Registry& registry) {
    std::vector<Entity> orderedEntities;
    orderedEntities.reserve(registry.transforms.getAll().size());

    for (const auto& [entity, transformComponent] : registry.transforms.getAll()) {
        (void)transformComponent;
        orderedEntities.push_back(entity);
    }

    std::sort(orderedEntities.begin(), orderedEntities.end());

    for (Entity entity : orderedEntities) {
        if (registry.names.has(entity) && registry.names.get(entity).name == "Parent") {
            return entity;
        }
    }

    for (Entity entity : orderedEntities) {
        if (!registry.hierarchy.has(entity) || registry.hierarchy.get(entity).parent == INVALID_ENTITY) {
            return entity;
        }
    }

    return orderedEntities.empty() ? INVALID_ENTITY : orderedEntities.front();
}

static void updateWindowTitle(GLFWwindow* window, double fps) {
    const std::string title = "SuGar Engine | FPS: " + std::to_string(static_cast<int>(fps + 0.5));
    glfwSetWindowTitle(window, title.c_str());
}

// --- SuGarApp Implementation ---

SuGarApp::SuGarApp() {
    // Build the gameplay pipeline up front so the editor can introspect it (the
    // Systems panel) before Play ever runs. Idempotent.
    setupSystemSchedule();
}

SuGarApp::~SuGarApp() {
    cleanup();
}

void SuGarApp::run() {
    // Install the crash reporter first, before anything can fault. Writes a minidump +
    // text report to ./crashes on an unhandled exception; a no-op on non-Windows. Cheap
    // provenance for when a game under test dies deep into a session (see CrashHandler.h).
    CrashHandler::install("crashes");

    // SUGAR_GAME=<dir> boots an external game (scene + assets) instead of the built-in
    // demo (M4 dogfood: games live outside the engine repo). Recorded here, consumed by
    // initScene. No chdir -- engine resources stay repo/exe-relative; only content moves.
    if (const char* gameEnv = std::getenv("SUGAR_GAME")) {
        gameDirectory = gameEnv;
        std::cout << "[game] SUGAR_GAME = " << gameDirectory << "\n";
    }

    // Opt-in editor-command self-test (Phase 11A). Runs and exits early so it can
    // be checked in CI / by hand without spinning up Vulkan.
    if (std::getenv("SUGAR_SELFTEST") != nullptr) {
        SelfTests::run();
        return;
    }

    // Opt-in headless profiling (Phase 14C). Measures snapshot memory / restore /
    // query / physics / scheduler on a representative scene, then exits — the
    // evidence for whether binary/delta snapshots are worth building.
    if (std::getenv("SUGAR_BENCH") != nullptr) {
        Benchmarks::run();
        return;
    }

    // Opt-in QA / stress harness (Phase QA): invariant checks at scale + edge
    // inputs (grid vs brute force, determinism, patch/id churn). Headless, exits.
    if (std::getenv("SUGAR_STRESS") != nullptr) {
        StressTests::run();
        return;
    }

    // Opt-in RmlUi integration smoke test (Phase 16B.1): verifies the RmlUi
    // build/link/init path headless, before the Vulkan render interface exists.
    if (std::getenv("SUGAR_UITEST") != nullptr) {
        if (!RuntimeUIView::smokeTest()) {
            std::exit(1);
        }
        return;
    }

    // Opt-in headless cook (Phase 19B): scan the asset tree, cook every asset that
    // has a cooker, and exit. No window, no Vulkan device — which is the whole reason
    // AssetCooker was kept device-free, and what lets the build pipeline (the next M3
    // item) run "cook, then build" instead of inventing a second path.
    if (std::getenv("SUGAR_COOK") != nullptr) {
        AssetDatabase database;
        database.scan("assets");
        AssetCooker::setDatabase(&database);

        for (const std::string& problem : database.getProblems()) {
            std::cerr << "[cook] catalog: " << problem << "\n";
        }

        std::vector<std::string> errors;
        const int cooked = AssetCooker::cookAll(database, errors);
        for (const std::string& error : errors) {
            std::cerr << "[cook] " << error << "\n";
        }

        std::cout << "[cook] " << database.getAssets().size() << " assets catalogued, "
                  << cooked << " cooked, " << errors.size() << " failed\n";
        std::cout << "[cook] cache: " << AssetCooker::cacheDirectory() << "\n";
        std::exit(errors.empty() ? 0 : 1);
    }

    // Opt-in headless standalone export (Phase 20, DevDocs/DESIGN_PACKAGING.md): cook the
    // assets a scene reaches, write the manifest, copy the scene, and exit. No window,
    // no Vulkan device -- the same reason the cooker is device-free. Runtime binaries
    // are the build pipeline's job (the next M3 item), so this gate ships assets +
    // scene + manifest; a real release adds the exe and DLLs to Spec::binaries.
    if (std::getenv("SUGAR_PACKAGE") != nullptr) {
        // SUGAR_GAME packages an external game (assets/scene/DLL from the game folder, out
        // to <gameDir>/dist); without it, the repo's demo scene packages to build/package.
        const char* gameEnv = std::getenv("SUGAR_GAME");
        const std::string root = gameEnv != nullptr ? std::string(gameEnv) : std::string(".");
        const std::string assetsDir = gameEnv != nullptr ? (root + "/assets") : "assets";
        const std::string scenePath = gameEnv != nullptr ? (root + "/scene.json") : "scene.json";

        AssetDatabase database;
        database.scan(assetsDir);

        Packager::Spec spec;
        if (std::filesystem::exists(scenePath)) {
            spec.scenes.push_back(scenePath);
        }
        // A game acquires assets by KEY at runtime (AssetGateway) — a voxel game's block
        // atlas is referenced by no scene at all, because the chunks that use it do not
        // exist until the world generates. The scene-reachability walk cannot see those,
        // so packaging a game shipped a manifest with zero textures in it and the
        // standalone had nothing to load. For an external game the content set is the
        // game's own assets folder: everything it scanned ships. (Unreal's "additional
        // asset directories to cook" and Unity's Resources folder answer the same
        // question; the engine's demo package stays scene-reachability-only.)
        if (gameEnv != nullptr) {
            for (const AssetEntry& entry : database.getAssets()) {
                if (entry.type == AssetType::Texture || entry.type == AssetType::Model ||
                    entry.type == AssetType::Audio) {
                    spec.extraAssetKeys.push_back(entry.key);
                }
            }
        }

        // Phase 21: ship the runtime too, so the package is a runnable standalone rather
        // than assets alone. This is what Phase 20 deferred to the build pipeline.
        spec.binaries = Packager::collectRuntimeBinaries();
        if (gameEnv != nullptr) {
            const std::string gameDll = root + "/Game.dll"; // the game's behaviours
            if (std::filesystem::exists(gameDll)) {
                spec.binaries.push_back(gameDll);
            }
        }

        // Loose runtime files the standalone needs but that aren't cooked assets: the
        // compiled shaders, the runtime-UI font, and the game's UI documents. Each keeps
        // the exact path the runtime looks for it under.
        {
            std::error_code ec;
            for (const auto& entry : std::filesystem::directory_iterator("build/shaders", ec)) {
                if (entry.path().extension() == ".spv") {
                    spec.extraFiles.push_back({ entry.path().generic_string(),
                                                "build/shaders/" + entry.path().filename().string() });
                }
            }
            const std::string font = "assets/fonts/LatoLatin-Regular.ttf";
            if (std::filesystem::exists(font)) {
                spec.extraFiles.push_back({ font, font });
            }
            // Branding: the packaged window wears the same icon the editor does. Not a
            // cooked asset — nothing references it from a scene, the engine loads it by
            // this exact path.
            for (const char* art : { "assets/branding/sugar_cube.png",
                                     "assets/branding/sugar_logo.png" }) {
                if (std::filesystem::exists(art)) {
                    spec.extraFiles.push_back({ art, art });
                }
            }
            const std::string uiDir = gameEnv != nullptr ? (root + "/assets/ui") : "assets/ui";
            for (const auto& entry : std::filesystem::directory_iterator(uiDir, ec)) {
                if (entry.is_regular_file()) {
                    spec.extraFiles.push_back({ entry.path().generic_string(),
                                                "assets/ui/" + entry.path().filename().string() });
                }
            }
        }
        spec.outputDirectory = gameEnv != nullptr ? (root + "/dist") : "build/package";

        const Packager::Report report = Packager::package(database, spec);
        for (const std::string& key : report.unpackagedKeys) {
            std::cerr << "[package] not cooked (source-backed): " << key << "\n";
        }
        for (const std::string& error : report.errors) {
            std::cerr << "[package] " << error << "\n";
        }
        std::cout << "[package] " << report.scenesPackaged << " scene(s), "
                  << report.assetsPackaged << " cooked asset(s), "
                  << report.sourceModelsCopied << " source model(s), "
                  << report.binariesCopied << " binaries, "
                  << report.extraFilesCopied << " runtime file(s) -> " << spec.outputDirectory << "\n";

        // Acceptance check: resolve the package the way the shipped exe will -- manifest
        // only, no source. A package that does not verify is not shippable, so this
        // gates the pipeline's exit code (DevDocs/DESIGN_BUILD_PIPELINE.md).
        bool verified = report.ok();
        if (report.ok()) {
            std::vector<std::string> verifyErrors;
            verified = Packager::verify(spec.outputDirectory, verifyErrors);
            for (const std::string& error : verifyErrors) {
                std::cerr << "[package] " << error << "\n";
            }
            std::cout << "[package] verify: " << (verified ? "OK" : "FAILED")
                      << " (resolved every manifest key with no source)\n";
        }
        std::exit(verified ? 0 : 1);
    }

    // Single confidence entry point: run every correctness gate and exit nonzero
    // if any failed (for CI). Deliberately only aggregates real pass/fail gates —
    // self-tests + stress; benchmarks are measurements, not gates, so they stay
    // separate under SUGAR_BENCH.
    if (std::getenv("SUGAR_VALIDATE") != nullptr) {
        std::cout << "=== SuGar validate: correctness gates ===\n";
        const auto self = SelfTests::run();
        std::cout << "\n";
        const auto stress = StressTests::run();

        const int passed = self.first + stress.first;
        const int total = self.second + stress.second;
        const int failures = total - passed;
        std::cout << "\n[validate] self-tests:   " << self.first << "/" << self.second
                  << (self.first == self.second ? "  PASS" : "  FAIL") << "\n";
        std::cout << "[validate] stress-tests: " << stress.first << "/" << stress.second
                  << (stress.first == stress.second ? "  PASS" : "  FAIL") << "\n";
        std::cout << "[validate] benchmarks:   measurement only (run SUGAR_BENCH=1)\n";
        std::cout << "[validate] === " << passed << "/" << total << " checks passed, "
                  << failures << " failure(s) ===\n";
        std::exit(failures == 0 ? 0 : 1);
    }

    initWindow();
    initVulkan();
    initAudio();
    initScene();
    initRenderer();
    mainLoop();
    cleanup();
}

void SuGarApp::initAudio() {
    // Best-effort: if no playback device is available the engine keeps running
    // silently. Audio is independent of Vulkan, so this can fail without
    // affecting rendering or gameplay.
    audioEngine.init();
}

void SuGarApp::initWindow() {
    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window = glfwCreateWindow(WIDTH, HEIGHT, "SuGar Engine", nullptr, nullptr);
    setWindowIcon();
    Input::init();

    glfwSetKeyCallback(window, [](GLFWwindow*, int key, int, int action, int) {
        if (action == GLFW_PRESS || action == GLFW_REPEAT) {
            Input::setKey(key, true);
        } else if (action == GLFW_RELEASE) {
            Input::setKey(key, false);
        }
    });

    glfwSetCursorPosCallback(window, [](GLFWwindow*, double x, double y) {
        Input::setMousePosition(x, y);
    });

    glfwSetMouseButtonCallback(window, [](GLFWwindow*, int button, int action, int) {
        if (action == GLFW_PRESS) {
            Input::setMouseButton(button, true);
        } else if (action == GLFW_RELEASE) {
            Input::setMouseButton(button, false);
        }
    });

    // Typed characters for runtime UI text entry. Installed before ImGui's backend,
    // which chains to it, so both the editor and the game see text.
    glfwSetCharCallback(window, [](GLFWwindow*, unsigned int codepoint) {
        // Minimal UTF-8 encode; the runtime UI only needs printable input for now.
        std::string utf8;
        if (codepoint < 0x80) {
            utf8 += static_cast<char>(codepoint);
        } else if (codepoint < 0x800) {
            utf8 += static_cast<char>(0xC0 | (codepoint >> 6));
            utf8 += static_cast<char>(0x80 | (codepoint & 0x3F));
        } else if (codepoint < 0x10000) {
            utf8 += static_cast<char>(0xE0 | (codepoint >> 12));
            utf8 += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
            utf8 += static_cast<char>(0x80 | (codepoint & 0x3F));
        }
        Input::pushText(utf8);
    });

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void SuGarApp::initScene() {
    // Behaviors now live in the hot-swappable game module DLL; load it and let it
    // register them into Core's BehaviorRegistry. The engine still runs (behaviors
    // just inert) if the module is missing.
    // Behaviours DLL: an external game (editor) loads its own Game.dll from the game
    // folder; a packaged standalone loads Game.dll from beside the exe (packaging copies
    // it there); the bare editor/demo loads the built-in SuGarGame. The game's C++ never
    // enters the engine repo either way.
    const bool packagedStandalone =
        AssetManifest::existsAt(Packager::manifestPath(".")) && std::filesystem::exists("scene.json");
    if (!gameDirectory.empty()) {
        gameModule.load("Game", gameDirectory);
    } else if (packagedStandalone) {
        gameModule.load("Game");
    } else {
        gameModule.load("SuGarGame");
    }
    InputActions::registerDefaults();
    registry.reset();
    sceneLights.clear();
    drawList.items.clear();
    drawList.lights.clear();
    orbitParent = INVALID_ENTITY;

    // Packaged vs editor is decided by one fact: does a manifest sit next to the
    // executable (DevDocs/DESIGN_PACKAGING.md)? If so, this is a shipped build with no
    // source tree -- resolve every asset key through the manifest and cook nothing.
    // Otherwise it is the editor: scan the source assets and cook on demand.
    const std::string manifestFile = Packager::manifestPath(".");
    packagedBuild = AssetManifest::existsAt(manifestFile);
    // Time-travel is an editor affordance: never capture in a shipped build, and pause
    // capture once a single snapshot costs more than the budget (a large scene) so the
    // game stays playable. 4 ms/step is a small fraction of the 16.6 ms fixed step.
    snapshotPolicy.configure(4.0, packagedBuild);
    if (AssetManifest::existsAt(manifestFile)) {
        std::string manifestError;
        if (packageManifest.load(manifestFile, manifestError)) {
            AssetCooker::setManifest(&packageManifest);
            AssetCooker::setCacheDirectory(Packager::cacheDirectory("."));
            CrashHandler::setPackage(manifestFile);
            std::cout << "[package] running from manifest: " << packageManifest.size()
                      << " asset(s)\n";
        } else {
            std::cerr << "[package] " << manifestError << " -- falling back to source\n";
        }
    }

    // Game content root: an external game's assets live under <gameDirectory>/assets;
    // the editor/demo uses the repo's "assets". AssetPath anchors every key at the
    // "assets/" segment, so a game asset at an absolute path still spells the same key a
    // scene references -- which is exactly what lets the catalog resolve it.
    const std::string assetRoot =
        gameDirectory.empty() ? std::string("assets") : (gameDirectory + "/assets");

    if (!AssetCooker::hasManifest()) {
        assetDatabase.scan(assetRoot);
        fileWatcher.watch(assetRoot);
        // The cooker takes cook keys from the catalog when it has them (19B). Set
        // before the first load below: every mesh/texture/audio load now goes through
        // a cooked artifact, and without the catalog the cooker would re-hash each
        // source file itself -- same answer, more I/O.
        AssetCooker::setDatabase(&assetDatabase);
    }

    // Boot a scene FILE instead of the built-in demo when one is available: an external
    // game (SUGAR_GAME) in the editor, or a packaged standalone (a scene.json ships beside
    // the manifest next to the exe). The demo is only for the bare editor with no game.
    std::string sceneFile;
    std::string saveDir;
    if (!gameDirectory.empty()) {
        sceneFile = gameDirectory + "/scene.json";
        saveDir = gameDirectory;
    } else if (AssetCooker::hasManifest() && std::filesystem::exists("scene.json")) {
        sceneFile = "scene.json";
        saveDir = ".";
    }

    if (!sceneFile.empty()) {
        // Persistent store (high scores, progress) lives beside the scene.
        SaveData::setPath(saveDir + "/save.dat");
        SaveData::load();

        if (SceneSerializer::load(registry, sceneLights, sceneFile)) {
            runningGame = true; // drives the 2D camera + auto-play below
            // A sane default camera target so the game is visible without further setup.
            for (const auto& [entity, transformComponent] : registry.transforms.getAll()) {
                (void)transformComponent;
                orbitParent = entity;
                break;
            }
            CrashHandler::setScene(sceneFile);
            std::cout << "[game] booted " << sceneFile << "\n";
        } else {
            std::cerr << "[game] FAILED to load " << sceneFile << " -- no scene.\n";
        }
        return;
    }

    const std::string cubeMeshPath = resolveAssetPath("assets/models/textured_cube.obj");
    const std::string checkerTexturePath = resolveAssetPath("assets/textures/checker.png");

    const Entity parentCube = registry.createEntity();
    registry.names.add(parentCube, { "Parent" });
    registry.transforms.add(parentCube, { Transform{
        {0.0f, 0.0f, 0.0f},
        glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
        {1.10f, 1.10f, 1.10f}
    } });
    registry.meshes.add(parentCube, { ResourceManager::loadMesh(cubeMeshPath) });
    registry.materials.add(parentCube, { Material{
        ResourceManager::loadTexture(checkerTexturePath),
        0.05f,
        0.82f,
        1.0f
    } });
    registry.hierarchy.add(parentCube, {});
    registry.scripts.add(parentCube, { "Spinner" });
    orbitParent = parentCube;

    // Phase 9 demo: a looping ambient pad that starts on Play. Marked spatial so
    // it attenuates with the Player's distance (the Player carries the listener).
    {
        AudioSourceComponent ambience{};
        ambience.clip = ResourceManager::loadAudioClip(resolveAssetPath("assets/audio/ambient_loop.wav"));
        ambience.volume = 0.7f;
        ambience.loop = true;
        ambience.playOnStart = true;
        ambience.spatial = true;
        registry.audioSources.add(parentCube, ambience);
    }

    const Entity childCube = registry.createEntity();
    registry.names.add(childCube, { "Child" });
    registry.transforms.add(childCube, { Transform{
        {2.0f, 0.0f, 0.0f},
        quatFromEulerXYZ({0.0f, -0.55f, 0.0f}),
        {0.75f, 0.75f, 0.75f}
    } });
    registry.meshes.add(childCube, { ResourceManager::loadMesh(cubeMeshPath) });
    registry.materials.add(childCube, { Material{
        ResourceManager::loadTexture(checkerTexturePath),
        0.95f,
        0.16f,
        1.0f
    } });
    registry.hierarchy.add(childCube, {});
    registry.setParent(childCube, parentCube);

    const Entity supportCube = registry.createEntity();
    registry.names.add(supportCube, { "Support" });
    registry.transforms.add(supportCube, { Transform{
        {-1.75f, 0.65f, -0.9f},
        quatFromEulerXYZ({0.25f, 0.35f, 0.15f}),
        {0.55f, 0.55f, 0.55f}
    } });
    registry.meshes.add(supportCube, { ResourceManager::loadMesh(cubeMeshPath) });
    registry.materials.add(supportCube, { Material{
        ResourceManager::loadTexture(checkerTexturePath),
        0.30f,
        0.45f,
        1.0f
    } });
    registry.hierarchy.add(supportCube, {});
    registry.setParent(supportCube, parentCube);

    // Free-standing, player-controlled cube (arrow keys in Play mode). Top-level
    // so its movement is independent of the spinning parent.
    const Entity playerCube = registry.createEntity();
    registry.names.add(playerCube, { "Player" });
    registry.transforms.add(playerCube, { Transform{
        {0.0f, -1.6f, 1.5f},
        glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
        {0.6f, 0.6f, 0.6f}
    } });
    registry.meshes.add(playerCube, { ResourceManager::loadMesh(cubeMeshPath) });
    registry.materials.add(playerCube, { Material{
        ResourceManager::loadTexture(checkerTexturePath),
        0.10f,
        0.35f,
        1.0f
    } });
    registry.hierarchy.add(playerCube, {});
    registry.scripts.add(playerCube, { "PlayerController" });
    // The Player is the "ears": drive it with the arrow keys in Play mode to hear
    // the spatial ambience on the Parent grow and fade with distance.
    registry.audioListeners.add(playerCube, { 1.0f });

    // Dynamic rigid body that falls under gravity in Play mode (7B adds a ground
    // for it to land on). Top-level so the world-space integration is correct.
    const Entity fallingBox = registry.createEntity();
    registry.names.add(fallingBox, { "FallingBox" });
    registry.transforms.add(fallingBox, { Transform{
        {-0.5f, 3.0f, 0.0f},
        glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
        {0.5f, 0.5f, 0.5f}
    } });
    registry.meshes.add(fallingBox, { ResourceManager::loadMesh(cubeMeshPath) });
    registry.materials.add(fallingBox, { Material{
        ResourceManager::loadTexture(checkerTexturePath),
        0.20f,
        0.55f,
        1.0f
    } });
    registry.hierarchy.add(fallingBox, {});
    registry.rigidBodies.add(fallingBox, RigidBodyComponent{});
    ColliderComponent fallingBoxCollider{};
    fallingBoxCollider.type = ColliderType::Box;
    fallingBoxCollider.halfExtents = { 0.5f, 0.5f, 0.5f };
    registry.colliders.add(fallingBox, fallingBoxCollider);
    // Phase 9A demo: landing sound via CollisionEvent -> CollisionSfx -> Audio.
    // The blip is a one-shot (playOnStart = false), fired when the box hits ground.
    registry.scripts.add(fallingBox, { "CollisionSfx" });
    {
        AudioSourceComponent thud{};
        thud.clip = ResourceManager::loadAudioClip(resolveAssetPath("assets/audio/blip.wav"));
        thud.volume = 0.9f;
        thud.playOnStart = false;
        thud.spatial = true;
        registry.audioSources.add(fallingBox, thud);
    }

    // Static ground plane for dynamic bodies to land on.
    const Entity ground = registry.createEntity();
    registry.names.add(ground, { "Ground" });
    registry.transforms.add(ground, { Transform{
        {0.0f, -2.5f, 0.0f},
        glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
        {10.0f, 0.5f, 10.0f}
    } });
    registry.meshes.add(ground, { ResourceManager::loadMesh(cubeMeshPath) });
    registry.materials.add(ground, { Material{
        ResourceManager::loadTexture(checkerTexturePath),
        0.0f,
        0.9f,
        1.0f
    } });
    registry.hierarchy.add(ground, {});
    RigidBodyComponent groundBody{};
    groundBody.isStatic = true;
    groundBody.useGravity = false;
    registry.rigidBodies.add(ground, groundBody);
    ColliderComponent groundCollider{};
    groundCollider.type = ColliderType::Box;
    groundCollider.halfExtents = { 0.5f, 0.5f, 0.5f };
    registry.colliders.add(ground, groundCollider);

    // Bouncy box with an initial sideways velocity: shows restitution (bounces)
    // and friction (horizontal slide decays) on the same body.
    const Entity bouncyBox = registry.createEntity();
    registry.names.add(bouncyBox, { "BouncyBox" });
    registry.transforms.add(bouncyBox, { Transform{
        {1.5f, 2.0f, -1.0f},
        glm::quat{1.0f, 0.0f, 0.0f, 0.0f},
        {0.4f, 0.4f, 0.4f}
    } });
    registry.meshes.add(bouncyBox, { ResourceManager::loadMesh(cubeMeshPath) });
    registry.materials.add(bouncyBox, { Material{
        ResourceManager::loadTexture(checkerTexturePath),
        0.60f,
        0.30f,
        1.0f
    } });
    registry.hierarchy.add(bouncyBox, {});
    RigidBodyComponent bouncyBody{};
    bouncyBody.velocity = { 1.5f, 0.0f, 0.0f };
    bouncyBody.restitution = 0.8f;
    bouncyBody.friction = 0.3f;
    registry.rigidBodies.add(bouncyBox, bouncyBody);
    ColliderComponent bouncyCollider{};
    bouncyCollider.type = ColliderType::Box;
    bouncyCollider.halfExtents = { 0.5f, 0.5f, 0.5f };
    registry.colliders.add(bouncyBox, bouncyCollider);

    sceneLights.push_back({
        {2.5f, 2.5f, 3.0f},
        {1.0f, 0.95f, 0.85f}
    });
    sceneLights.push_back({
        {-2.5f, 1.5f, 2.0f},
        {0.35f, 0.45f, 1.0f}
    });
    sceneLights.push_back({
        {0.0f, 4.0f, -3.5f},
        {0.85f, 0.55f, 0.45f}
    });

    // UIRoot: the singleton entity owning global runtime-UI state (Phase 16A/16B.3).
    // It carries a transform only so it participates in scene serialization the same
    // way every other entity does — the runtime UI is driven by the components.
    const Entity uiRoot = registry.createEntity();
    registry.names.add(uiRoot, { "UIRoot" });
    registry.transforms.add(uiRoot, {});
    registry.hierarchy.add(uiRoot, {});
    UIScreenComponent uiScreen{};
    uiScreen.screenStack = { "HUD" };
    registry.uiScreens.add(uiRoot, uiScreen);
    registry.focus.add(uiRoot, {});
    // Two text fields, each its own entity, tied to a document element by id. Typing
    // routes to whichever one FocusComponent points at — routing decided in ECS.
    const Entity nameField = registry.createEntity();
    registry.names.add(nameField, { "UIField_Name" });
    registry.transforms.add(nameField, {});
    registry.hierarchy.add(nameField, {});
    registry.textInputs.add(nameField, { "name", "", 0 });
    registry.setParent(nameField, uiRoot);

    const Entity tagField = registry.createEntity();
    registry.names.add(tagField, { "UIField_Tag" });
    registry.transforms.add(tagField, {});
    registry.hierarchy.add(tagField, {});
    registry.textInputs.add(tagField, { "tag", "", 0 });
    registry.setParent(tagField, uiRoot);

    CrashHandler::setScene("(built-in demo scene)");
}

void SuGarApp::rebuildDrawList() {
    const glm::vec3 cameraPosition = renderer ? renderer->getCameraWorldPosition() : glm::vec3(0.0f);
    buildDrawListFromECS(registry, sceneLights, cameraPosition, drawList);
}

void SuGarApp::updateCameraTargets() {
    if (!renderer) {
        return;
    }

    // A game camera (Core CameraComponent) supersedes the editor orbit/follow rig:
    // the active camera entity's world transform *is* the view (position = world
    // translation, forward = rotation * -Z). Lowest active entity id wins, for the
    // same determinism reason the rest of the engine sorts by id. Absent ⇒ fall
    // through to the orbit/follow behavior below, so non-game scenes are unchanged.
    if (runningGame) {
        Entity cameraEntity = INVALID_ENTITY;
        for (const auto& [entity, camera] : registry.cameras.getAll()) {
            if (camera.active && registry.transforms.has(entity) &&
                (cameraEntity == INVALID_ENTITY || entity < cameraEntity)) {
                cameraEntity = entity;
            }
        }
        if (cameraEntity != INVALID_ENTITY) {
            const glm::mat4 world = getWorldMatrix(cameraEntity, registry);
            const glm::mat3 basis(world);
            const glm::vec3 position = glm::vec3(world[3]);
            const glm::vec3 forward = glm::normalize(basis * glm::vec3(0.0f, 0.0f, -1.0f));
            const glm::vec3 up = glm::normalize(basis * glm::vec3(0.0f, 1.0f, 0.0f));
            const CameraComponent& camera = registry.cameras.get(cameraEntity);
            renderer->setScriptedCamera(position, forward, up,
                                        camera.fovDegrees, camera.nearPlane, camera.farPlane);
            return;
        }
    }

    if (orbitParent == INVALID_ENTITY || !registry.transforms.has(orbitParent)) {
        renderer->setOrbitTarget(glm::vec3(0.0f, 0.0f, 0.0f));
        renderer->setFollowTargetPosition(glm::vec3(0.0f, 0.0f, 0.0f));
        return;
    }

    const glm::vec3 targetPosition = getWorldPosition(orbitParent, registry);
    renderer->setOrbitTarget(targetPosition);
    renderer->setFollowTargetPosition(targetPosition);
}

void SuGarApp::refreshSceneVisualsKeepEditor() {
    // Registry contents changed; re-derive everything downstream of them. Any
    // voices from the previous state reference stale playback, so silence them.
    // Deliberately does NOT touch editor selection / undo — entity ids are intact
    // (in-place restore) or the caller clears editor state separately.
    audioEngine.stopAll();
    orbitParent = findOrbitParentEntity(registry);
    rebuildDrawList();
    if (renderer) {
        renderer->setDrawList(&drawList);
        renderer->refreshDrawListResources();
    }
    updateCameraTargets();
}

void SuGarApp::onSceneReplaced() {
    // The registry contents were swapped wholesale (entity ids reassigned), so the
    // editor's selection + undo history — which reference ids — must be discarded.
    refreshSceneVisualsKeepEditor();
    if (renderer) {
        renderer->clearEditorState();
    }
}

void SuGarApp::play() {
    if (engineState != EngineState::Edit) {
        return;
    }

    sceneSnapshot = SceneSerializer::saveToString(registry, sceneLights);
    if (sceneSnapshot.empty()) {
        std::cerr << "failed to snapshot scene; staying in Edit mode\n";
        return;
    }

    engineState = EngineState::Play;
    snapshots->clear();
    bookmarks.clear();
    scrubCursor = -1;
    snapshotPolicy.reset();       // re-arm for this session (a fresh scene may fit)
    captureSnapshotBudgeted();    // record the initial play state as frame 0 (if enabled)
    std::cout << "[Play] entered play mode\n";
}

void SuGarApp::pause() {
    if (engineState == EngineState::Play) {
        engineState = EngineState::Paused;
        audioEngine.setPaused(true);
        std::cout << "[Pause] gameplay paused\n";
    }
}

void SuGarApp::resume() {
    if (engineState == EngineState::Paused) {
        if (scrubCursor >= 0) {
            resumeLive(); // leave time-travel scrubbing back to the live edge
        } else {
            engineState = EngineState::Play;
            audioEngine.setPaused(false);
        }
        std::cout << "[Play] gameplay resumed\n";
    }
}

void SuGarApp::stop() {
    if (engineState == EngineState::Edit) {
        return;
    }

    // Silence all gameplay audio before the snapshot restore wipes the live
    // components; the restored sources come back with started=false.
    audioEngine.stopAll();
    audioEngine.setPaused(false);

    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
    }

    // Phase 14A: patch the pre-play snapshot back into the live entities when the
    // structure is unchanged (the common case — Play only mutates components), so
    // the selection and undo history the user had in Edit survive Stop. A
    // structural change during Play falls back to the full rebuild.
    snapshots->clear();
    bookmarks.clear();
    scrubCursor = -1;
    if (SceneSerializer::patchFromString(registry, sceneLights, sceneSnapshot)) {
        refreshSceneVisualsKeepEditor();
    } else if (SceneSerializer::loadFromString(registry, sceneLights, sceneSnapshot)) {
        onSceneReplaced();
    } else {
        std::cerr << "failed to restore scene snapshot\n";
    }
    engineState = EngineState::Edit;
    std::cout << "[Stop] restored edit scene\n";
}

void SuGarApp::captureSnapshotBudgeted() {
    if (!snapshotPolicy.enabled()) {
        return; // packaged build, or auto-paused for a too-large scene
    }
    const auto start = std::chrono::high_resolution_clock::now();
    captureSnapshot();
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start).count();
    snapshotPolicy.recordCaptureCost(ms);
    if (!snapshotPolicy.enabled()) {
        // Just crossed the budget: drop the partial history so the Timeline shows a
        // clean "paused" state rather than a misleading truncated ring.
        snapshots->clear();
        std::cout << "[Play] " << snapshotPolicy.disabledReason() << "\n";
    }
}

void SuGarApp::captureSnapshot() {
    std::string snapshot = SceneSerializer::saveToString(registry, sceneLights);
    if (snapshot.empty()) {
        return;
    }
    snapshots->push(snapshot);

    // Drop bookmarks whose frame has scrolled out of the retained window.
    if (snapshots->count() > 0 && !bookmarks.empty()) {
        const uint64_t oldest = snapshots->frameNumber(0);
        for (auto it = bookmarks.begin(); it != bookmarks.end();) {
            it = (it->first < oldest) ? bookmarks.erase(it) : std::next(it);
        }
    }
}

void SuGarApp::restoreSnapshot(const std::string& snapshot) {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
    }
    // Phase 14A: try to patch the snapshot into the existing entities (preserves
    // ids, so editor selection / inspector / undo survive a scrub or Stop). Only
    // a structural change forces the old destroy-and-rebuild path, which clears
    // editor state because ids are reassigned.
    if (SceneSerializer::patchFromString(registry, sceneLights, snapshot)) {
        refreshSceneVisualsKeepEditor();
        return;
    }
    if (!SceneSerializer::loadFromString(registry, sceneLights, snapshot)) {
        std::cerr << "failed to restore snapshot\n";
        return;
    }
    onSceneReplaced();
}

void SuGarApp::advanceOneFixedStep() {
    updateSystems(FIXED_TIMESTEP);
    captureSnapshotBudgeted();
}

void SuGarApp::scrubTo(int index) {
    if (snapshots->count() == 0) {
        return;
    }
    index = std::clamp(index, 0, snapshots->count() - 1);
    if (index == scrubCursor) {
        return; // avoid re-restoring the same frame while dragging the slider
    }

    scrubCursor = index;
    engineState = EngineState::Paused;
    audioEngine.stopAll();
    audioEngine.setPaused(true);
    restoreSnapshot(snapshots->get(index));
}

void SuGarApp::stepFrame(int delta) {
    if (engineState == EngineState::Edit || snapshots->count() == 0) {
        return;
    }

    if (scrubCursor >= 0) {
        scrubTo(scrubCursor + delta); // move within the recorded ring
        return;
    }

    // At the live edge.
    if (delta < 0) {
        scrubTo(snapshots->count() - 2); // step back into history
    } else {
        engineState = EngineState::Paused; // frame-by-frame forward: advance one step
        audioEngine.setPaused(false);
        advanceOneFixedStep();
        audioEngine.setPaused(true);
    }
}

float SuGarApp::fixedTimestep() const {
    return FIXED_TIMESTEP;
}

void SuGarApp::reloadGameModule() {
    // Time the swap itself (copy + FreeLibrary + LoadLibrary + re-register), i.e.
    // the latency the engine owns — the compile that precedes it isn't ours to
    // measure. This is the live counterpart to the headless SUGAR_BENCH numbers.
    const auto start = std::chrono::high_resolution_clock::now();
    const bool ok = gameModule.reload();
    const double milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start).count();
    if (ok) {
        std::cout << "[GameModule] hot reload complete (" << std::fixed << std::setprecision(2)
                  << milliseconds << " ms swap)\n";
    } else {
        std::cerr << "[GameModule] hot reload failed\n";
    }
}

void SuGarApp::resumeLive() {
    if (scrubCursor >= 0 && snapshots->count() > 0) {
        restoreSnapshot(snapshots->get(snapshots->count() - 1));
    }
    scrubCursor = -1;
    engineState = EngineState::Play;
    audioEngine.setPaused(false);
}

int SuGarApp::currentFrameIndex() const {
    const int total = snapshots->count();
    if (total == 0) {
        return -1;
    }
    return scrubCursor >= 0 ? scrubCursor : total - 1;
}

void SuGarApp::setBookmark(const std::string& label) {
    const int index = currentFrameIndex();
    if (index < 0) {
        return;
    }
    const uint64_t frame = snapshots->frameNumber(index);
    if (label.empty()) {
        bookmarks.erase(frame);
    } else {
        bookmarks[frame] = label;
    }
}

bool SuGarApp::isFrameBookmarked(int index) const {
    if (index < 0 || index >= snapshots->count()) {
        return false;
    }
    return bookmarks.count(snapshots->frameNumber(index)) > 0;
}

std::string SuGarApp::bookmarkLabel(int index) const {
    if (index < 0 || index >= snapshots->count()) {
        return {};
    }
    const auto it = bookmarks.find(snapshots->frameNumber(index));
    return it == bookmarks.end() ? std::string{} : it->second;
}

int SuGarApp::bookmarkCount() const {
    return static_cast<int>(bookmarks.size());
}

void SuGarApp::jumpBookmark(int direction) {
    const int current = currentFrameIndex();
    if (current < 0) {
        return;
    }
    const int total = snapshots->count();
    if (direction > 0) {
        for (int i = current + 1; i < total; ++i) {
            if (isFrameBookmarked(i)) { scrubTo(i); return; }
        }
    } else {
        for (int i = current - 1; i >= 0; --i) {
            if (isFrameBookmarked(i)) { scrubTo(i); return; }
        }
    }
}

void SuGarApp::setupSystemSchedule() {
    if (systemScheduleReady) {
        return;
    }

    // Script driver: runs every entity's named Behavior. Behaviors are
    // unconstrained gameplay code, so this declares a broad write set — anything
    // a behavior might touch. That honest declaration is what keeps it ordered
    // ahead of physics/audio (they conflict) rather than falsely "independent".
    systemSchedule.add(System{
        "Script",
        maskOf(ComponentType::Script, ComponentType::Transform, ComponentType::RigidBody,
               ComponentType::Collider, ComponentType::AudioSource, ComponentType::GameData),
        maskOf(ComponentType::Script, ComponentType::Transform, ComponentType::RigidBody,
               ComponentType::AudioSource, ComponentType::GameData),
        [this](float dt) {
            // Sorted-snapshot iteration + spawn/destroy safety live in Core's
            // ScriptSystem::run so they are headless-testable (see testScriptSystem).
            ScriptSystem::run(registry, dt);
        }});

    // Navigation: plans a route for any agent whose destination changed this step,
    // then walks it. Between Script and Animation on purpose — gameplay decides
    // *where* to go, navigation moves the character there, and animation then
    // depicts the movement rather than racing it. Ahead of physics for the same
    // reason animation is: a navigated position should be an input to this step's
    // collision, not a step stale.
    systemSchedule.add(System{
        "Navigation",
        // NavObstacle and Hierarchy are read, not written: avoidance resolves each
        // obstacle's world position, which walks parent transforms. The honest
        // declaration, per the 13B lesson Audio taught.
        maskOf(ComponentType::NavAgent, ComponentType::Transform,
               ComponentType::NavObstacle, ComponentType::Hierarchy),
        maskOf(ComponentType::NavAgent, ComponentType::Transform),
        [this](float dt) {
            NavigationSystem::update(registry, dt);
        }});

    // Animation: advances each player's authoritative time and writes the sampled
    // pose into transforms. Ahead of physics on purpose — a clip-driven transform
    // should be an input to this step's collision, not a step stale. Name and
    // Hierarchy are declared because resolving a track's target walks the subtree
    // by name (the honest declaration, per the 13B lesson that Audio taught).
    systemSchedule.add(System{
        "Animation",
        maskOf(ComponentType::Animation, ComponentType::Name, ComponentType::Hierarchy,
               ComponentType::AnimationParameters),
        maskOf(ComponentType::Animation, ComponentType::Transform, ComponentType::AnimationState),
        [this](float dt) {
            // Single-clip players first, then state machines. One system rather than
            // two because they write the same storage (Transform) and would be
            // ordered anyway — declaring them separately would claim an independence
            // the scheduler would immediately have to take back.
            AnimationSystem::update(registry, dt);
            AnimationStateSystem::update(registry, dt);
        }});

    // Physics: integrates bodies and resolves collisions on the same fixed step,
    // accumulating this step's contacts for the dispatch system below.
    systemSchedule.add(System{
        "Physics",
        maskOf(ComponentType::Collider, ComponentType::Transform, ComponentType::RigidBody),
        maskOf(ComponentType::Transform, ComponentType::RigidBody),
        [this](float dt) {
            physicsWorld.step(registry, dt);
        }});

    // Collision dispatch: routes this step's contacts to the behaviors on each
    // involved entity. Behaviors were started by the Script system, so onCollision
    // can safely mutate components (e.g. request a one-shot sound) before audio.
    systemSchedule.add(System{
        "CollisionDispatch",
        maskOf(ComponentType::Script, ComponentType::Transform, ComponentType::AudioSource),
        maskOf(ComponentType::Transform, ComponentType::RigidBody, ComponentType::AudioSource),
        [this](float) {
            auto dispatchCollision = [&](Entity entity, const CollisionEvent& event) {
                if (entity == INVALID_ENTITY || !registry.scripts.has(entity)) {
                    return;
                }
                // Read the behavior name through a const view: dispatch inspects
                // ScriptComponent, it doesn't mutate it (Phase 13B enforcement).
                const Registry& readOnly = registry;
                if (Behavior* behavior = BehaviorRegistry::get(readOnly.scripts.get(entity).behavior)) {
                    behavior->onCollision(registry, entity, event);
                }
            };
            for (const CollisionEvent& event : physicsWorld.getCollisionEvents()) {
                dispatchCollision(event.a, event);
                dispatchCollision(event.b, event);
            }
        }});

    // Audio last: positions are final for this step, so spatial attenuation,
    // playOnStart triggers, and collision one-shots use up-to-date state.
    // Hierarchy is declared because spatial attenuation resolves world positions
    // via getWorldPosition, which walks parent transforms — a real dependency that
    // 13A's declaration missed and 13B's enforcement surfaced.
    systemSchedule.add(System{
        "Audio",
        maskOf(ComponentType::Transform, ComponentType::Hierarchy,
               ComponentType::AudioListener, ComponentType::AudioSource),
        maskOf(ComponentType::AudioSource),
        [this](float) {
            AudioSystem::update(registry, audioEngine);
        }});

    // Runtime UI: drains intents queued at render rate and applies them to the
    // authoritative UI components. Runs on the fixed step so UI-state changes are
    // deterministic and replayable, exactly like input-driven behaviors.
    systemSchedule.add(System{
        "RuntimeUI",
        0,
        maskOf(ComponentType::UIScreen, ComponentType::Focus, ComponentType::TextInput),
        [this](float) {
            RuntimeUISystem::update(registry, uiIntents);
        }});

    // Guard rail on by default in Debug (tracking is compiled out of Release, so
    // this is inert there). Default is Warn: violations surface in the editor
    // Systems panel without halting the session. SUGAR_STRICT escalates to
    // fail-fast — the first undeclared access throws (with a stderr message),
    // which surfaces as a nonzero exit for headless/CI runs.
    if (ComponentAccess::trackingEnabled()) {
        if (std::getenv("SUGAR_STRICT") != nullptr) {
            systemSchedule.setEnforcement(AccessEnforcement::Strict);
        } else {
            systemSchedule.setEnforcement(AccessEnforcement::Warn);
            systemSchedule.setViolationHandler([](const AccessViolation&) {}); // panel-only, no stderr
        }
    }

    systemScheduleReady = true;
}

void SuGarApp::updateSystems(float fixedDeltaTime) {
    // Gameplay is a declared pipeline (Phase 13A): script -> physics -> collision
    // dispatch -> audio, each with its read/write sets. The scheduler runs them in
    // deterministic registration order; the declared sets drive independence
    // analysis (stages()) for future parallelism, not reordering.
    setupSystemSchedule();
    systemSchedule.run(fixedDeltaTime);
}

void SuGarApp::initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createCommandPool();
    ResourceManager::init(device, physicalDevice, commandPool, graphicsQueue);
    // How long a released GPU resource must outlive its last reference
    // (DevDocs/DESIGN_GPU_RETIREMENT.md). The renderer states its own depth rather than
    // ResourceManager assuming one.
    ResourceManager::setFramesInFlight(static_cast<uint32_t>(Renderer::framesInFlight()));
    // Engine wires the ECS's asset-release hook to ResourceManager, so Core's
    // Registry never references the Vulkan-coupled resource system directly.
    registry.onReleaseAsset = [](AssetHandle handle) { ResourceManager::release(handle); };

    // Symmetric ACQUIRE half of the dependency-inversion asset seam (M4 L3): game code
    // links only Core and cannot see ResourceManager, so it acquires renderable assets
    // by key through AssetGateway, whose backend we wire here. loadMesh/loadTexture/
    // loadAudioClip already dedup by key and incref; the release above balances them on
    // entity destroy. Resolve failures are swallowed to INVALID_HANDLE so a game never
    // faults on a missing key — it degrades (no mesh) instead.
    AssetGateway::install(AssetGateway::Backend{
        [](const std::string& key) -> AssetHandle {
            try { return ResourceManager::loadMesh(key); } catch (...) { return INVALID_HANDLE; }
        },
        [](const std::string& key) -> AssetHandle {
            try { return ResourceManager::loadTexture(key); } catch (...) { return INVALID_HANDLE; }
        },
        [](const std::string& key) -> AssetHandle {
            try { return ResourceManager::loadAudioClip(key); } catch (...) { return INVALID_HANDLE; }
        },
        [](const RuntimeMeshData& data) -> AssetHandle {
            std::string error;
            const AssetHandle handle = ResourceManager::createRuntimeMesh(data, error);
            if (handle == INVALID_HANDLE && !error.empty()) {
                std::cerr << "[assets] createMesh rejected: " << error << "\n";
            }
            return handle;
        },
        [](AssetHandle handle) { ResourceManager::release(handle); },
    });

    createCommandBuffers();
}

void SuGarApp::initRenderer() {
    rebuildDrawList();
    renderer = std::make_unique<Renderer>(this);
    renderer->setWindow(window);
    renderer->setAssetDatabase(&assetDatabase);
    renderer->setRegistry(&registry);
    renderer->setSystemSchedule(&systemSchedule);
    renderer->setUIIntentQueue(&uiIntents); // UI callbacks emit intents into this
    renderer->setDrawList(&drawList);
    renderer->init();

    // A game frames its own scene: a FREE camera on +Z looking down -Z at the XY play
    // plane, which is how the Level-1 2D games are laid out. The demo keeps its orbit
    // camera. (Scene-authored cameras are the eventual replacement -- friction log.)
    if (runningGame) {
        renderer->setCameraMode(CameraMode::FREE);
        renderer->setCameraPose(glm::vec3(0.0f, 0.0f, 12.0f), -90.0f, 0.0f);
    }

    // A packaged standalone hides all editor chrome and shows only the game viewport +
    // HUD. An editor-run game (SUGAR_GAME, no manifest) keeps the editor for debugging.
    renderer->setGameView(AssetCooker::hasManifest());

    updateCameraTargets();
}

void SuGarApp::mainLoop() {
    double lastTime = glfwGetTime();
    double fpsTimer = lastTime;
    int framesThisSecond = 0;
    bool reloadDescriptors = false;

    // A booted game starts playing immediately: behaviours only run in Play, and someone
    // launching a game expects it running, not paused in the editor. The editor (no
    // SUGAR_GAME) still opens in Edit. (Auto-play, M4 friction #6.)
    if (runningGame) {
        play();
    }

    while (!glfwWindowShouldClose(window)) {
        double currentTime = glfwGetTime();
        float deltaTime = static_cast<float>(currentTime - lastTime);
        lastTime = currentTime;

        Input::beginFrame();
        glfwPollEvents();
        processInput(deltaTime);

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth == 0 || framebufferHeight == 0) {
            glfwWaitEventsTimeout(0.05);
            continue;
        }

        // The editor requests a reimport; the engine performs it — here, outside the
        // render frame and with the device idle, because a reload destroys GPU
        // resources. Same AssetReimport call the watcher makes, with force=true: a
        // developer pressing Reimport means it even when the bytes did not change.
        if (renderer != nullptr) {
            const std::string requestedReimport = renderer->takeAssetReimportRequest();
            if (!requestedReimport.empty()) {
                vkDeviceWaitIdle(device);
                const AssetReimport::Result reimported =
                    AssetReimport::reimport(assetDatabase, requestedReimport, true);
                if (!reimported.errorMessage.empty()) {
                    std::cerr << "reimport failed for '" << requestedReimport << "': "
                              << reimported.errorMessage << "\n";
                } else if (reimported.reloaded) {
                    reloadDescriptors = true;
                }
                assetDatabase.scan("assets");
                AssetCooker::discoverDependencies(reimported.assetKey);
            }
        }

        const auto changedFiles = fileWatcher.pollChanges();
        if (!changedFiles.empty()) {
            vkDeviceWaitIdle(device);

            bool anyReloaded = false;
            for (const auto& changedPath : changedFiles) {
                // One implementation of importing, shared with the editor's Reimport
                // button (19D). The watcher's job ends at "something changed"; what
                // that means is AssetReimport's, and force=false is what makes a touch
                // (mtime moved, bytes did not) cost nothing.
                const AssetReimport::Result reimported =
                    AssetReimport::reimport(assetDatabase, changedPath, false);

                if (!reimported.errorMessage.empty()) {
                    fileWatcher.markDirty(changedPath);
                    std::cerr << "failed to hot reload asset '" << changedPath << "': "
                              << reimported.errorMessage << "\n";
                    continue;
                }
                if (reimported.reloaded) {
                    std::cout << "Hot reloaded asset: " << reimported.assetKey << std::endl;
                    anyReloaded = true;
                }
            }

            assetDatabase.scan("assets");
            if (anyReloaded) {
                reloadDescriptors = true;
            }
        }

        // Fixed-timestep gameplay update. Gameplay advances only in Play state;
        // rendering below stays uncapped. The accumulator is clamped to avoid a
        // spiral of death after a long stall (e.g. window drag / hot reload).
        // Code hot reload: if the game DLL was recompiled, swap it in live. Done
        // here (outside the fixed-step update) so no behavior is mid-tick.
        if (gameModule.sourceChanged()) {
            reloadGameModule();
        }

        // Advance the sim only while live-playing (not while scrubbing history).
        if (engineState == EngineState::Play && scrubCursor < 0) {
            fixedAccumulator += deltaTime;
            if (fixedAccumulator > MAX_ACCUMULATED_TIME) {
                fixedAccumulator = MAX_ACCUMULATED_TIME;
            }
            while (fixedAccumulator >= FIXED_TIMESTEP) {
                updateSystems(FIXED_TIMESTEP);
                captureSnapshotBudgeted(); // record each fixed step for time travel (budget-gated)
                fixedAccumulator -= FIXED_TIMESTEP;
            }
        } else {
            fixedAccumulator = 0.0f;
        }

        updateCameraTargets();
        rebuildDrawList();
        if (reloadDescriptors) {
            renderer->refreshDrawListResources();
            reloadDescriptors = false;
        }
        renderer->drawFrame();

        framesThisSecond++;
        const double fpsWindow = currentTime - fpsTimer;
        if (fpsWindow >= 1.0) {
            const double fps = static_cast<double>(framesThisSecond) / fpsWindow;
            updateWindowTitle(window, fps);
            // Opt-in measurement (SUGAR_FPSLOG=1): emit FPS + drawn-entity count to
            // stderr each second. A dogfood-driven stand-in for the missing in-game
            // profiler overlay — lets a scaling run be measured, not guessed.
            if (std::getenv("SUGAR_FPSLOG") != nullptr) {
                // Both numbers, because they answer different questions: `items` is what
                // the scene asked to draw, `drawCalls` is what the GPU was actually told
                // to do. Instancing lives in the gap, and a run reporting only the first
                // cannot tell whether any batching happened.
                std::cerr << "[fps] " << fps
                          << " entities=" << registry.transforms.getAll().size()
                          << " items=" << drawList.items.size()
                          << " drawCalls=" << renderer->submittedDrawCalls()
                          // Resource counts ride along, because the question a torture run
                          // asks — did thousands of spawn/destroy cycles leak anything? — is
                          // answered by these staying flat, and nothing headless can see them.
                          << " meshes=" << ResourceManager::liveMeshCount()
                          << " textures=" << ResourceManager::liveTextureCount()
                          << " clips=" << ResourceManager::liveAudioClipCount()
                          << " retired=" << ResourceManager::retiredCount() << "\n";
            }
            fpsTimer = currentTime;
            framesThisSecond = 0;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void SuGarApp::processInput(float deltaTime) {
    const bool imguiReady = ImGui::GetCurrentContext() != nullptr;
    const bool captureKeyboard = imguiReady && ImGui::GetIO().WantCaptureKeyboard;
    const bool captureMouse = imguiReady && ImGui::GetIO().WantCaptureMouse;
    const bool viewportHovered = renderer != nullptr && renderer->isViewportHovered();

    if (Input::isKeyDown(GLFW_KEY_ESCAPE)) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }

    // Function-key shortcuts are deliberately NOT gated on ImGui's WantCaptureKeyboard.
    // The editor is one big ImGui dockspace, so WantCaptureKeyboard is true whenever an
    // ImGui window has focus — i.e. essentially always — which silently killed every
    // F-key shortcut below. ImGui never consumes function keys for text entry, so the
    // guard bought nothing. Character keys (camera 1/2/3 etc.) stay gated: those really
    // do conflict with typing in an ImGui field.

    if (Input::isKeyPressed(GLFW_KEY_F5)) {
        if (!SceneSerializer::save(registry, sceneLights, "scene.json")) {
            std::cerr << "failed to save scene.json\n";
        }
    }

    if (Input::isKeyPressed(GLFW_KEY_F9)) {
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
        }

        if (SceneSerializer::load(registry, sceneLights, "scene.json")) {
            CrashHandler::setScene("scene.json");
            onSceneReplaced();
        } else {
            std::cerr << "failed to load scene.json\n";
        }
    }

    // First-person cursor capture. The game *requests* it through Core Input; the engine
    // grants it only while actually playing, so stopping (F6) always hands the cursor
    // back and a game can never trap the user in the editor.
    {
        const bool wantCapture = Input::cursorCaptured() && engineState == EngineState::Play;
        if (wantCapture != cursorCaptureApplied) {
            cursorCaptureApplied = wantCapture;
            glfwSetInputMode(window, GLFW_CURSOR,
                             wantCapture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        }
    }

    // Play-mode control (F6 toggle Play/Stop, F7 toggle Pause/Resume).
    // The editor toolbar (Phase 5C) calls the same play()/stop()/pause() methods.
    if (Input::isKeyPressed(GLFW_KEY_F6)) {
        if (engineState == EngineState::Edit) {
            play();
        } else {
            stop();
        }
    }

    if (Input::isKeyPressed(GLFW_KEY_F7)) {
        if (engineState == EngineState::Play) {
            pause();
        } else if (engineState == EngineState::Paused) {
            resume();
        }
    }

    // F8: manually hot-reload the game module DLL (recompiled behaviors).
    if (Input::isKeyPressed(GLFW_KEY_F8)) {
        reloadGameModule();
    }

    // Runtime UI navigation (Phase 16B.3). Input never mutates UI state directly —
    // it queues an intent here at render rate, and the RuntimeUI system applies it
    // on the next fixed step. F1 opens a screen, F2 goes back.
    if (Input::isKeyPressed(GLFW_KEY_F1)) {
        uiIntents.push(UIIntent::openScreen("Inventory"));
    }
    if (Input::isKeyPressed(GLFW_KEY_F2)) {
        uiIntents.push(UIIntent::popScreen());
    }

    // Runtime UI keyboard navigation. Tab/Shift+Tab move focus, Enter activates the
    // focused element. Both go through intents, so focus lives in ECS and survives
    // snapshot restore like any other authoritative UI state.
    if (renderer != nullptr) {
        if (Input::isKeyPressed(GLFW_KEY_TAB)) {
            const bool reverse = Input::isKeyDown(GLFW_KEY_LEFT_SHIFT) ||
                                 Input::isKeyDown(GLFW_KEY_RIGHT_SHIFT);
            renderer->runtimeUIFocusNext(reverse);
        }
        if (Input::isKeyPressed(GLFW_KEY_ENTER)) {
            renderer->runtimeUIActivateFocused();
        }
    }

    // Text entry: typed characters become intents, so the authoritative buffer lives
    // in TextInputComponent (and survives snapshot restore) rather than inside an
    // RmlUi text field. Only while playing — the editor owns the keyboard in Edit.
    if (engineState != EngineState::Edit) {
        if (!Input::textThisFrame().empty()) {
            uiIntents.push(UIIntent::appendText(Input::textThisFrame()));
        }
        if (Input::isKeyPressed(GLFW_KEY_BACKSPACE)) {
            uiIntents.push(UIIntent::backspaceText());
        }
        if (Input::isKeyPressed(GLFW_KEY_LEFT)) {
            uiIntents.push(UIIntent::caretLeft());
        }
        if (Input::isKeyPressed(GLFW_KEY_RIGHT)) {
            uiIntents.push(UIIntent::caretRight());
        }
    }

    // Pointer state for the runtime UI is fed from the Viewport panel (in
    // viewport-local coordinates), not from raw window coords — see Renderer.

    if (!captureKeyboard && Input::isKeyDown(GLFW_KEY_1)) {
        renderer->setCameraMode(CameraMode::FREE);
    }

    if (!captureKeyboard && Input::isKeyDown(GLFW_KEY_2)) {
        renderer->setCameraMode(CameraMode::ORBIT);
    }

    if (!captureKeyboard && Input::isKeyDown(GLFW_KEY_3)) {
        renderer->setCameraMode(CameraMode::FOLLOW);
    }

    if (!captureKeyboard && Input::isKeyDown(GLFW_KEY_W)) {
        renderer->moveCameraForward(deltaTime);
    }

    if (!captureKeyboard && Input::isKeyDown(GLFW_KEY_S)) {
        renderer->moveCameraBackward(deltaTime);
    }

    if (!captureKeyboard && Input::isKeyDown(GLFW_KEY_A)) {
        renderer->moveCameraLeft(deltaTime);
    }

    if (!captureKeyboard && Input::isKeyDown(GLFW_KEY_D)) {
        renderer->moveCameraRight(deltaTime);
    }

    const glm::vec2 mouseDelta = Input::getMouseDelta();
    if ((!captureMouse || viewportHovered) && (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f)) {
        renderer->rotateCamera(mouseDelta.x, -mouseDelta.y);
    }
}

void SuGarApp::cleanup() {
    if (cleanedUp) {
        return;
    }

    audioEngine.shutdown();
    renderer.reset();

    if (device != VK_NULL_HANDLE) {
        orbitParent = INVALID_ENTITY;
        registry.reset();
        sceneLights.clear();
        drawList.items.clear();
        drawList.lights.clear();
        ResourceManager::shutdown();
    }

    if (commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }

    if (device != VK_NULL_HANDLE) {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }

#ifndef NDEBUG
    if (instance != VK_NULL_HANDLE && debugMessenger != VK_NULL_HANDLE) {
        DestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        debugMessenger = VK_NULL_HANDLE;
    }
#endif

    if (instance != VK_NULL_HANDLE && surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }

    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }

    if (window != nullptr) {
        glfwDestroyWindow(window);
        window = nullptr;
        glfwTerminate();
    }

    cleanedUp = true;
}

// --- Vulkan Setup Methods ---

void SuGarApp::createInstance() {
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("validation layers requested, but not available!");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "SuGar Engine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "SuGar Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("failed to create instance!");
    }

    std::cout << "Vulkan instance created successfully.\n";
}

void SuGarApp::setupDebugMessenger() {
    if (!enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    if (CreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger!");
    }

    std::cout << "Vulkan debug messenger created successfully.\n";
}

void SuGarApp::createSurface() {
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("failed to create Vulkan surface!");
    }

    std::cout << "Vulkan surface created successfully.\n";
}

void SuGarApp::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // Score GPUs to find the best one
    int bestScore = -1;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;

    for (const auto& device : devices) {
        if (!isDeviceSuitable(device)) {
            continue;
        }

        int score = 0;
        VkPhysicalDeviceProperties properties;
        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceProperties(device, &properties);
        vkGetPhysicalDeviceFeatures(device, &features);

        // Discrete GPU is much better than integrated
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
            score += 100;
        }

        // Maximum texture size is a good indicator of performance
        score += properties.limits.maxImageDimension2D;

        // Prefer devices with geometry shaders
        if (features.geometryShader) {
            score += 500;
        }

        if (score > bestScore) {
            bestScore = score;
            bestDevice = device;
        }
    }

    if (bestDevice == VK_NULL_HANDLE) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }

    physicalDevice = bestDevice;

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    CrashHandler::setGpu(properties.deviceName);
    std::cout << "Selected GPU: " << properties.deviceName << " (Score: " << bestScore << ")\n";
    std::cout << "GPU Type: ";
    switch (properties.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            std::cout << "Discrete GPU (dGPU)";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            std::cout << "Integrated GPU (iGPU)";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            std::cout << "Virtual GPU";
            break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:
            std::cout << "CPU";
            break;
        default:
            std::cout << "Other";
            break;
    }
    std::cout << "\n";
}

void SuGarApp::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice, surface);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(), indices.presentFamily.value()};

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device!");
    }

    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);

    std::cout << "Logical device created.\n";
}

void SuGarApp::createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice, surface);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily.value();

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }

    std::cout << "Command pool created successfully.\n";
}

void SuGarApp::createCommandBuffers() {
    // Frame-based renderer: allocate one command buffer per frame in flight,
    // NOT per swapchain image.
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)commandBuffers.size();

    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }

    std::cout << "Command buffers allocated: " << commandBuffers.size() << "\n";
}

// --- Helper Methods ---

bool SuGarApp::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool layerFound = false;

        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

std::vector<const char*> SuGarApp::getRequiredExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);

    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    return extensions;
}

bool SuGarApp::isDeviceSuitable(VkPhysicalDevice device) {
    QueueFamilyIndices indices = findQueueFamilies(device, surface);

    bool extensionsSupported = checkDeviceExtensionSupport(device);

    bool swapChainAdequate = false;
    if (extensionsSupported) {
        SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device, surface);
        swapChainAdequate = !swapChainSupport.formats.empty() && !swapChainSupport.presentModes.empty();
    }

    return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

bool SuGarApp::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

QueueFamilyIndices SuGarApp::findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);

        if (presentSupport) {
            indices.presentFamily = i;
        }

        if (indices.isComplete()) {
            break;
        }

        i++;
    }

    return indices;
}

SwapChainSupportDetails SuGarApp::querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapChainSupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }

    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);

    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }

    return details;
}

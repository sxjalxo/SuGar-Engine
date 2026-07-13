#include "SuGarApp.h"
#include "Renderer.h"
#include "assets/ResourceManager.h"
#include "audio/AudioSystem.h"
#include "SelfTests.h"
#include "core/Input.h"
#include "core/InputActions.h"
#include "rendering/Camera.h"
#include "rendering/Material.h"
#include "scene/Behavior.h"
#include "scene/BehaviorRegistry.h"
#include "scene/DrawList.h"
#include "scene/SceneSerializer.h"
#include "scene/TransformMath.h"
#include "imgui.h"
#include <stdexcept>
#include <iostream>
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
    const std::vector<std::string> candidates = {
        relativePath,
        "../" + relativePath,
        "../../" + relativePath
    };

    for (const auto& candidate : candidates) {
        std::ifstream file(candidate);
        if (file.good()) {
            return candidate;
        }
    }

    return relativePath;
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
    // Opt-in editor-command self-test (Phase 11A). Runs and exits early so it can
    // be checked in CI / by hand without spinning up Vulkan.
    if (std::getenv("SUGAR_SELFTEST") != nullptr) {
        SelfTests::run();
        return;
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

    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void SuGarApp::initScene() {
    // Behaviors now live in the hot-swappable game module DLL; load it and let it
    // register them into Core's BehaviorRegistry. The engine still runs (behaviors
    // just inert) if the module is missing.
    gameModule.load("SuGarGame");
    InputActions::registerDefaults();
    registry.reset();
    sceneLights.clear();
    drawList.items.clear();
    drawList.lights.clear();
    orbitParent = INVALID_ENTITY;
    assetRegistry.scan("assets");
    fileWatcher.watch("assets");

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
}

void SuGarApp::rebuildDrawList() {
    buildDrawListFromECS(registry, sceneLights, drawList);
}

void SuGarApp::updateCameraTargets() {
    if (!renderer) {
        return;
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
    captureSnapshot(); // record the initial play state as frame 0
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
    captureSnapshot();
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
    if (gameModule.reload()) {
        std::cout << "[GameModule] hot reload complete\n";
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
               ComponentType::Collider, ComponentType::AudioSource),
        maskOf(ComponentType::Script, ComponentType::Transform, ComponentType::RigidBody,
               ComponentType::AudioSource),
        [this](float dt) {
            // Behaviors are stateless and shared; per-entity lifecycle state
            // (`started`) lives in the component so it survives snapshot/restore.
            for (auto& [entity, scriptComponent] : registry.scripts.getAll()) {
                Behavior* behavior = BehaviorRegistry::get(scriptComponent.behavior);
                if (behavior == nullptr) {
                    continue;
                }
                if (!scriptComponent.started) {
                    behavior->onStart(registry, entity);
                    scriptComponent.started = true;
                }
                behavior->onUpdate(registry, entity, dt);
            }
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
    // Engine wires the ECS's asset-release hook to ResourceManager, so Core's
    // Registry never references the Vulkan-coupled resource system directly.
    registry.onReleaseAsset = [](AssetHandle handle) { ResourceManager::release(handle); };
    createCommandBuffers();
}

void SuGarApp::initRenderer() {
    rebuildDrawList();
    renderer = std::make_unique<Renderer>(this);
    renderer->setWindow(window);
    renderer->setAssetRegistry(&assetRegistry);
    renderer->setRegistry(&registry);
    renderer->setSystemSchedule(&systemSchedule);
    renderer->setDrawList(&drawList);
    renderer->init();
    updateCameraTargets();
}

void SuGarApp::mainLoop() {
    double lastTime = glfwGetTime();
    double fpsTimer = lastTime;
    int framesThisSecond = 0;
    bool reloadDescriptors = false;

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

        const auto changedFiles = fileWatcher.pollChanges();
        if (!changedFiles.empty()) {
            vkDeviceWaitIdle(device);

            bool anyReloaded = false;
            for (const auto& path : changedFiles) {
                try {
                    if (ResourceManager::reloadAsset(path)) {
                        std::cout << "Hot reloaded asset: " << path << std::endl;
                        anyReloaded = true;
                    }
                } catch (const std::exception& exception) {
                    fileWatcher.markDirty(path);
                    std::cerr << "failed to hot reload asset '" << path << "': " << exception.what() << "\n";
                }
            }

            assetRegistry.scan("assets");
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
                captureSnapshot(); // record each fixed step for time travel
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
            updateWindowTitle(window, static_cast<double>(framesThisSecond) / fpsWindow);
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

    if (!captureKeyboard && Input::isKeyPressed(GLFW_KEY_F5)) {
        if (!SceneSerializer::save(registry, sceneLights, "scene.json")) {
            std::cerr << "failed to save scene.json\n";
        }
    }

    if (!captureKeyboard && Input::isKeyPressed(GLFW_KEY_F9)) {
        if (device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(device);
        }

        if (SceneSerializer::load(registry, sceneLights, "scene.json")) {
            onSceneReplaced();
        } else {
            std::cerr << "failed to load scene.json\n";
        }
    }

    // Play-mode control (F6 toggle Play/Stop, F7 toggle Pause/Resume).
    // The editor toolbar (Phase 5C) calls the same play()/stop()/pause() methods.
    if (!captureKeyboard && Input::isKeyPressed(GLFW_KEY_F6)) {
        if (engineState == EngineState::Edit) {
            play();
        } else {
            stop();
        }
    }

    if (!captureKeyboard && Input::isKeyPressed(GLFW_KEY_F7)) {
        if (engineState == EngineState::Play) {
            pause();
        } else if (engineState == EngineState::Paused) {
            resume();
        }
    }

    // F8: manually hot-reload the game module DLL (recompiled behaviors).
    if (!captureKeyboard && Input::isKeyPressed(GLFW_KEY_F8)) {
        reloadGameModule();
    }

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

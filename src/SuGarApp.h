#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "assets/AssetDatabase.h"
#include "assets/AssetManifest.h"
#include "assets/FileWatcher.h"
#include "audio/AudioEngine.h"
#include <optional>
#include <string>
#include "core/EngineState.h"
#include "core/SnapshotStorage.h"
#include "core/SnapshotCapturePolicy.h"
#include "ecs/Registry.h"
#include "ecs/SystemSchedule.h"
#include "GameModuleLoader.h"
#include "physics/PhysicsWorld.h"
#include "scene/DrawList.h"
#include "scene/Light.h"
#include "ui/UIIntent.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

class Renderer;

struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> presentFamily;

    bool isComplete() const {
        return graphicsFamily.has_value() && presentFamily.has_value();
    }
};

struct SwapChainSupportDetails {
    VkSurfaceCapabilitiesKHR capabilities;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};

class SuGarApp {
public:
    SuGarApp();
    ~SuGarApp();

    void run();

    // Play-mode control. Driven by the editor toolbar (and keyboard shortcuts).
    // play() snapshots the live scene before mutation; stop() restores it.
    EngineState getEngineState() const { return engineState; }
    bool isPlaying() const { return engineState == EngineState::Play; }
    void play();
    void pause();
    void resume();
    void stop();

    // Time-travel debugging (Phase 11B). A ring buffer of full-scene snapshots is
    // captured each fixed step during Play; the editor Timeline panel drives these.
    int getSnapshotCount() const { return snapshots->count(); }
    // Time-travel capture status for the Timeline panel: false + a reason when capture
    // is off (packaged build, or auto-paused because the scene is too large).
    bool snapshotsEnabled() const { return snapshotPolicy.enabled(); }
    const std::string& snapshotDisabledReason() const { return snapshotPolicy.disabledReason(); }
    bool isScrubbing() const { return scrubCursor >= 0; }
    int getScrubCursor() const { return scrubCursor; }
    // Pauses and restores the snapshot at `index` (clamped) for inspection.
    void scrubTo(int index);
    // Steps by `delta` frames: within the ring while scrubbing; at the live edge,
    // -1 enters the ring and +1 advances the sim one fixed step (frame-by-frame).
    void stepFrame(int delta);
    // Leaves scrubbing: restores the newest snapshot and returns to live Play.
    void resumeLive();
    // Duration of one recorded frame, so the editor can show timeline seconds.
    float fixedTimestep() const;

    // Hot-reload the game module DLL (recompiled behaviors) without restarting.
    // Behaviors reconnect by name and component state is untouched.
    void reloadGameModule();

    // The gameplay system pipeline, for the editor Systems panel (read-only view
    // of declared read/write sets, parallel stages, and access violations).
    const SystemScheduler& getSystemSchedule() const { return systemSchedule; }

    // Timeline bookmarks: tag the current frame with a label and jump between
    // tagged frames. Bookmarks key off stable frame numbers, so they survive ring
    // eviction (until the tagged frame itself scrolls off the window).
    void setBookmark(const std::string& label); // empty label clears the current frame's bookmark
    bool isFrameBookmarked(int index) const;
    std::string bookmarkLabel(int index) const;
    int bookmarkCount() const;
    void jumpBookmark(int direction); // -1 = previous, +1 = next

    // Public getters for Renderer access
    VkDevice getDevice() const { return device; }
    VkCommandPool getCommandPool() const { return commandPool; }
    const std::vector<VkCommandBuffer>& getCommandBuffers() const { return commandBuffers; }
    VkQueue getGraphicsQueue() const { return graphicsQueue; }
    VkQueue getPresentQueue() const { return presentQueue; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkSurfaceKHR getSurface() const { return surface; }
    VkInstance getInstance() const { return instance; }

private:
    GLFWwindow* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue presentQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers;
    std::unique_ptr<Renderer> renderer;
    AssetDatabase assetDatabase;
    FileWatcher fileWatcher;

    // Populated only in a packaged build (a manifest sits next to the executable). It
    // outlives the AssetCooker pointer that references it, so it is a member, not a
    // local (DevDocs/DESIGN_PACKAGING.md).
    AssetManifest packageManifest;
    Registry registry;
    std::vector<Light> sceneLights;
    DrawList drawList;
    Entity orbitParent = INVALID_ENTITY;
    bool cleanedUp = false;
    EngineState engineState = EngineState::Edit;
    std::string sceneSnapshot;
    // Set from SUGAR_GAME: an external game directory the engine boots (scene.json +
    // assets/) instead of the built-in demo. Empty = editor / demo. Engine resources
    // (shaders, fonts, the game-behaviours DLL) stay anchored to the repo/exe, so only
    // game *content* comes from here.
    std::string gameDirectory;
    // True once a scene FILE was booted (external game or packaged standalone) rather than
    // the built-in demo -- gates the 2D game camera and auto-play.
    bool runningGame = false;
    float fixedAccumulator = 0.0f;
    PhysicsWorld physicsWorld;
    AudioEngine audioEngine;
    GameModuleLoader gameModule; // hot-swappable behaviors DLL

    // Runtime UI intents (Phase 16B.3): queued at render rate by input/UI callbacks,
    // drained on the fixed step by the RuntimeUI system so authoritative UI-state
    // changes stay deterministic. See DevDocs/DESIGN_RUNTIME_UI.md.
    UIIntentQueue uiIntents;

    // Phase 13A: the fixed-step gameplay pipeline as declared systems (script ->
    // physics -> collision dispatch -> audio) rather than a hardcoded sequence.
    // Built lazily on first update; run() executes them in deterministic order.
    SystemScheduler systemSchedule;
    bool systemScheduleReady = false;
    // Mirrors the GLFW cursor mode currently applied, so the input mode is only set when
    // it actually changes rather than every frame.
    bool cursorCaptureApplied = false;

    // Time-travel ring: snapshots behind an ISnapshotStorage (encoding-agnostic),
    // a scrub cursor (-1 = live), and bookmarks keyed by stable frame number.
    std::unique_ptr<ISnapshotStorage> snapshots = std::make_unique<JsonSnapshotStorage>(600); // ~10 s at 60 Hz
    int scrubCursor = -1;
    std::unordered_map<uint64_t, std::string> bookmarks;

    // Gates WHETHER a snapshot is captured each fixed step (the ring/format are
    // unchanged). Off in packaged builds; auto-pauses when a scene's per-step capture
    // blows the budget so a large game stays playable. See SnapshotCapturePolicy.
    SnapshotCapturePolicy snapshotPolicy;
    bool packagedBuild = false;

    void initWindow();
    void setWindowIcon();
    void initVulkan();
    void initAudio();
    // Time-travel internals.
    void captureSnapshot();
    // captureSnapshot() gated + timed by snapshotPolicy: no-op when capture is off,
    // and it disables further capture (clearing the ring) the step a snapshot first
    // blows the budget. All Play-path capture goes through this, not captureSnapshot.
    void captureSnapshotBudgeted();
    void advanceOneFixedStep();
    void restoreSnapshot(const std::string& snapshot);
    int currentFrameIndex() const; // scrub cursor, or newest when live
    void initScene();
    void initRenderer();
    void rebuildDrawList();
    void updateCameraTargets();
    // Re-derives orbit target, draw list, and GPU resources after the registry
    // contents change. refreshSceneVisualsKeepEditor leaves editor selection/undo
    // intact (used by in-place snapshot restore, where entity ids are preserved);
    // onSceneReplaced additionally clears editor state (used when the registry is
    // swapped wholesale and ids are reassigned).
    void refreshSceneVisualsKeepEditor();
    void onSceneReplaced();
    // Registers the fixed-step gameplay systems (with declared read/write sets)
    // into systemSchedule. Idempotent; called on first updateSystems.
    void setupSystemSchedule();
    // Advances gameplay by one fixed step. Runs only while in Play state.
    void updateSystems(float fixedDeltaTime);
    void mainLoop();
    void processInput(float deltaTime);
    void cleanup();
    
    // Vulkan functions
    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();
    void createCommandBuffers();
    
    // Helper functions
    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
    bool isDeviceSuitable(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);
};

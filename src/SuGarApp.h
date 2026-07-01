#pragma once

#include <vulkan/vulkan.h>
#include <deque>
#include <memory>
#include <vector>
#include "assets/AssetRegistry.h"
#include "assets/FileWatcher.h"
#include "audio/AudioEngine.h"
#include <optional>
#include <string>
#include "core/EngineState.h"
#include "ecs/Registry.h"
#include "physics/PhysicsWorld.h"
#include "scene/DrawList.h"
#include "scene/Light.h"

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
    int getSnapshotCount() const { return static_cast<int>(snapshotRing.size()); }
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
    AssetRegistry assetRegistry;
    FileWatcher fileWatcher;
    Registry registry;
    std::vector<Light> sceneLights;
    DrawList drawList;
    Entity orbitParent = INVALID_ENTITY;
    bool cleanedUp = false;
    EngineState engineState = EngineState::Edit;
    std::string sceneSnapshot;
    float fixedAccumulator = 0.0f;
    PhysicsWorld physicsWorld;
    AudioEngine audioEngine;

    // Time-travel ring buffer of serialized scene snapshots, oldest at front.
    std::deque<std::string> snapshotRing;
    size_t snapshotCapacity = 600; // ~10 s at 60 Hz
    int scrubCursor = -1;          // -1 = live; otherwise index into snapshotRing

    void initWindow();
    void initVulkan();
    void initAudio();
    // Time-travel internals.
    void captureSnapshot();
    void advanceOneFixedStep();
    void restoreSnapshot(const std::string& snapshot);
    void initScene();
    void initRenderer();
    void rebuildDrawList();
    void updateCameraTargets();
    // Re-derives orbit target, draw list, and GPU resources after the registry
    // contents are replaced wholesale (scene load or Play-mode restore).
    void onSceneReplaced();
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

#pragma once

#include <cstdint>
#include <glm/vec3.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include "assets/AssetHandle.h"
#include "ecs/Entity.h"
#include "editor/EditorCommand.h"
#include "scene/Transform.h"
#include "ui/RuntimeUIView.h"

class AssetRegistry;
class BasicTrianglePass;
class RenderPass;
class Registry;
class SystemScheduler;
class UIIntentQueue;
class SuGarApp;
enum class CameraMode : uint8_t;
struct DrawList;

struct GLFWwindow;

class Renderer {
public:
    Renderer(SuGarApp* app);
    ~Renderer();

    void init();
    void shutdown();

    void drawFrame();
    void setDrawList(const DrawList* drawList);
    void setAssetRegistry(AssetRegistry* assetRegistry) { this->assetRegistry = assetRegistry; }
    void setRegistry(Registry* registry) { this->registry = registry; }
    // Runtime UI callbacks emit into this queue; set before init().
    void setUIIntentQueue(UIIntentQueue* queue) { this->uiIntentQueue = queue; }
    // Records the player UI into the scene/viewport pass. Called by the scene pass
    // just before it ends, so the UI composites onto the game image.
    void renderRuntimeUIViewport(VkCommandBuffer cmd) { runtimeUI.render(cmd, viewportExtent, registry); }
    // Keyboard focus navigation for the player UI (emits intents; see RuntimeUIView).
    void runtimeUIFocusNext(bool reverse) { runtimeUI.focusNext(reverse); }
    void runtimeUIActivateFocused() { runtimeUI.activateFocused(); }
    void setSystemSchedule(const SystemScheduler* schedule) { this->systemSchedule = schedule; }
    void refreshDrawListResources();
    void moveCameraForward(float deltaTime);
    void moveCameraBackward(float deltaTime);
    void moveCameraLeft(float deltaTime);
    void moveCameraRight(float deltaTime);
    void rotateCamera(float xOffset, float yOffset);
    void setCameraMode(CameraMode mode);
    void setOrbitTarget(const glm::vec3& target);
    void setFollowTargetPosition(const glm::vec3& position);
    void renderImGui(VkCommandBuffer cmd);
    bool isViewportHovered() const { return viewportHovered; }
    // Resets per-scene editor state (selection, undo history, in-flight edits).
    // Called when the registry is replaced wholesale (Stop / scene load).
    void clearEditorState();
    
    // Window management
    void setWindow(GLFWwindow* window);
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    
    // Swapchain management
    void recreateSwapChain();
    
    // Getters for swapchain resources
    const std::vector<VkFramebuffer>& getSwapChainFramebuffers() const { return swapChainFramebuffers; }
    VkFormat getSwapChainImageFormat() const { return swapChainImageFormat; }
    VkExtent2D getSwapChainExtent() const { return swapChainExtent; }
    VkExtent2D getRenderExtent() const { return viewportExtent; }
    VkFramebuffer getViewportFramebuffer() const { return viewportFramebuffer; }
    VkFramebuffer getShadowFramebuffer() const { return shadowFramebuffer; }
    VkExtent2D getShadowExtent() const { return shadowExtent; }
    VkFormat getDepthFormat() const { return depthFormat; }
    VkFormat getShadowFormat() const { return shadowFormat; }
    VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }
    // Which frame-in-flight slot is being recorded. Passes that write host-visible
    // per-frame data need this to avoid overwriting a buffer the GPU is still
    // reading from the previous frame.
    uint32_t getCurrentFrame() const { return currentFrame; }
    static constexpr int framesInFlight() { return MAX_FRAMES_IN_FLIGHT; }
    VkDescriptorSet getDescriptorSet(AssetHandle textureHandle, uint32_t imageIndex) const;
    
private:
    void cleanupSwapChain();
    void createSwapChain();
    void createImageViews();
    void createUiRenderPass();
    void createDepthResources();
    void createShadowResources();
    void destroyShadowResources();
    void createFramebuffers();
    void createViewportResources();
    void destroyViewportResources();
    void updateViewportResourcesIfNeeded();
    void createSyncObjects();
    VkFormat findDepthFormat();
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
    bool hasStencilComponent(VkFormat format) const;
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    void createImGuiDescriptorPool();
    void initImGui();
    void shutdownImGui();
    void beginImGuiFrame();
    void buildEditorUi();
    void drawPlayControls();
    void drawTimelinePanel();
    void drawQueryConsolePanel();
    void drawSystemsPanel();
    void drawHierarchyPanel();
    void drawInspectorPanel();
    void drawAssetBrowserPanel();
    // Phase 18C. Navmesh list, bake statistics, and the explicit Rebake action.
    void drawNavigationPanel();
    // Phase 18C. Projects the navmesh and each agent's path onto the viewport image
    // with ImGui's draw list — no Vulkan pipeline, because this is *editor* chrome
    // and the editor is ImGui (RULES.md Rule 11). It also keeps the overlay a pure
    // read of ECS + the navmesh registry: it renders state, and owns none.
    void drawNavigationOverlay(float imageMinX, float imageMinY, float imageWidth, float imageHeight);
    // Casts a ray from the viewport pixel (relative to the image's top-left)
    // through the scene and returns the nearest entity hit, or INVALID_ENTITY.
    Entity pickEntityAt(float pixelX, float pixelY, float viewportWidth, float viewportHeight) const;
    // Draws the ImGuizmo manipulator over the viewport image and writes edits back
    // to the selected entity's local transform; records an undo command on release.
    void drawGizmo(float imageMinX, float imageMinY, float imageWidth, float imageHeight);
    // Deep-copies the selected entities' subtrees (records an undo command).
    void duplicateSelectedEntity();
    // Destroys the selected entities' subtrees (records an undo command).
    void deleteSelectedEntities();
    // Selection helpers (selectedEntity is the "primary"/active member of the set).
    void selectSingle(Entity entity);
    void toggleSelect(Entity entity);
    bool isSelected(Entity entity) const;
    std::vector<AssetHandle> collectDrawListTextures() const;
    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    
    // Vulkan helper functions (moved from SuGarApp)
    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };
    
    struct QueueFamilyIndices {
        std::optional<uint32_t> graphicsFamily;
        std::optional<uint32_t> presentFamily;
        bool isComplete() const {
            return graphicsFamily.has_value() && presentFamily.has_value();
        }
    };
    
    SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface);
    VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
    VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
    VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    
    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex);
    VkRenderPass getSceneRenderPass() const;
    VkRenderPass getShadowRenderPass() const;
    
    SuGarApp* app;
    GLFWwindow* window = nullptr;
    AssetRegistry* assetRegistry = nullptr;
    Registry* registry = nullptr;
    const SystemScheduler* systemSchedule = nullptr;
    const DrawList* drawList = nullptr;
    uint32_t currentFrame = 0;
    std::unique_ptr<RenderPass> mainRenderPass;
    BasicTrianglePass* activePass = nullptr;
    bool framebufferResized = false;
    Entity selectedEntity = INVALID_ENTITY;   // primary/active selection (inspector + gizmo)
    std::vector<Entity> selectedEntities;     // full multi-selection (includes the primary)
    bool viewportHovered = false;
    bool descriptorRefreshRequested = false;

    // Caches the Euler angles shown in the inspector so editing a quaternion
    // rotation doesn't jitter: the displayed Euler is only re-derived from the
    // quaternion when the selection changes or the quaternion is modified
    // externally (gizmo, undo), never every frame while the user is dragging.
    struct EditorTransformCache {
        Entity entity = INVALID_ENTITY;
        glm::vec3 eulerDegrees{0.0f};
        glm::quat quat{1.0f, 0.0f, 0.0f, 0.0f};
    };
    EditorTransformCache transformCache;

    // Undo/redo for editor mutations (transform edits, reparent, duplicate).
    CommandHistory commandHistory;

    // Which gizmo handle is active. Mapped to ImGuizmo enums in the .cpp so this
    // header doesn't depend on ImGuizmo.
    enum class GizmoOp { Translate, Rotate, Scale };
    GizmoOp gizmoOp = GizmoOp::Translate;
    bool gizmoWorldSpace = true;

    // Phase 18C overlay toggles. Off by default: a navmesh covers the whole floor,
    // so leaving it on would bury the scene you are trying to edit.
    bool showNavMesh = false;
    bool showNavPaths = true;

    // Transform-edit boundary capture (for undo). The gizmo and the inspector each
    // track their own drag so they never fight over the shared flag.
    bool gizmoEditing = false;
    Transform gizmoEditBefore;
    bool inspectorEditing = false;
    Transform inspectorEditBefore;

    // ECS query console (Phase 11B): last query text, its matches, and any error.
    char queryBuffer[128] = "rigidbody where vel.y < 0";
    std::vector<Entity> queryResults;
    std::string queryError;
    bool queryHasRun = false;

    // Timeline bookmark label being entered.
    char bookmarkLabelBuffer[64] = "";
    std::string editorStatusMessage;
    std::vector<AssetHandle> deferredResourceReleases;
    
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    
    // Sync objects
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    
    // Swapchain resources (owned by Renderer)
    VkSwapchainKHR swapChain = VK_NULL_HANDLE;
    std::vector<VkImage> swapChainImages;
    VkFormat swapChainImageFormat;
    VkExtent2D swapChainExtent;
    std::vector<VkImageView> swapChainImageViews;
    std::vector<VkFramebuffer> swapChainFramebuffers;
    
    // Depth buffer
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D viewportExtent{0, 0};
    VkExtent2D requestedViewportExtent{0, 0};
    bool viewportResourcesDirty = false;
    VkImage viewportImage = VK_NULL_HANDLE;
    VkDeviceMemory viewportImageMemory = VK_NULL_HANDLE;
    VkImageView viewportImageView = VK_NULL_HANDLE;
    VkFramebuffer viewportFramebuffer = VK_NULL_HANDLE;
    VkImage shadowImage = VK_NULL_HANDLE;
    VkDeviceMemory shadowImageMemory = VK_NULL_HANDLE;
    VkImageView shadowImageView = VK_NULL_HANDLE;
    VkFramebuffer shadowFramebuffer = VK_NULL_HANDLE;
    VkSampler shadowSampler = VK_NULL_HANDLE;
    VkExtent2D shadowExtent{2048, 2048};
    VkFormat shadowFormat = VK_FORMAT_UNDEFINED;

    // Descriptor sets
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::unordered_map<AssetHandle, std::vector<VkDescriptorSet>> textureDescriptorSets;

    // ImGui editor
    VkRenderPass uiRenderPass = VK_NULL_HANDLE;
    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;
    RuntimeUIView runtimeUI; // player-facing UI (RmlUi); ImGui above is the editor
    UIIntentQueue* uiIntentQueue = nullptr;
    VkDescriptorSet viewportTextureDescriptor = VK_NULL_HANDLE;
    bool imguiInitialized = false;
};

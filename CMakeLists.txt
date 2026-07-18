cmake_minimum_required(VERSION 3.21)
project(SuGarEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

# Colocate the exe and all DLLs (SuGarCore, SuGarGame) in one dir so the Windows
# loader finds them at runtime. CMake appends the per-config subdir (e.g. /Debug).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_subdirectory(external/glfw)

# --- RmlUi (runtime player UI, Phase 16B) -----------------------------------
# Player-facing UI (HTML/CSS), separate from the Dear ImGui editor. Linked into the
# engine executable only -- never into SuGarCore (Rule 15 keeps Core free of platform
# UI). RmlUi needs a font engine even for headless initialisation, so FreeType is
# vendored below and the built-in FreeType font engine stays enabled.
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# FreeType: RmlUi's font rasterizer (solved-infrastructure dep, RULES-compliant).
# Built from external/freetype and exposed under the target name RmlUi expects.
# Optional FreeType deps (zlib/png/harfbuzz/brotli) stay disabled -- not needed for
# glyph rasterization.
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
add_subdirectory(external/freetype)
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

add_subdirectory(external/RmlUi)

add_library(imgui
    external/imgui/imgui.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_glfw.cpp
    external/imgui/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC
    external/imgui
    external/imgui/backends
    external/glfw/include
)

target_link_libraries(imgui PUBLIC
    glfw
    Vulkan::Vulkan
)

# --- Core layer -------------------------------------------------------------
# Engine-agnostic foundation: ECS, component data, math, behavior registry, input.
# No Vulkan / renderer / ResourceManager dependencies (the Registry releases assets
# through an injected hook). The hot-reloadable game module (Phase 12B) will link
# only against this. STATIC for now; becomes SHARED once the game DLL needs to
# share Core singletons (e.g. BehaviorRegistry's table).
add_library(SuGarCore SHARED
    src/ecs/Entity.h
    src/ecs/EntityManager.cpp
    src/ecs/EntityManager.h
    src/ecs/ComponentAccess.cpp
    src/ecs/ComponentAccess.h
    src/ecs/ComponentStorage.h
    src/ecs/Components.h
    src/ecs/Registry.h
    src/ecs/SystemSchedule.h
    src/core/Input.cpp
    src/core/Input.h
    src/core/InputActions.cpp
    src/core/InputActions.h
    src/scene/Behavior.h
    src/scene/BehaviorRegistry.cpp
    src/scene/BehaviorRegistry.h
    src/scene/Transform.h
    src/scene/TransformMath.h
    src/physics/CollisionEvent.h
    src/physics/PhysicsComponents.h
    src/animation/AnimationClip.h
    src/animation/AnimationClip.cpp
    src/animation/AnimationClipRegistry.h
    src/animation/AnimationClipRegistry.cpp
    src/animation/AnimationComponents.h
    src/animation/AnimationSystem.h
    src/animation/AnimationSystem.cpp
    src/animation/AnimationGraph.h
    src/animation/AnimationGraphRegistry.h
    src/animation/AnimationGraphRegistry.cpp
    src/animation/AnimationStateSystem.h
    src/animation/AnimationStateSystem.cpp
    src/animation/Pose.h
    src/animation/Pose.cpp
    src/animation/Skin.h
    src/animation/SkinRegistry.h
    src/animation/SkinRegistry.cpp
    src/animation/Skinning.h
    src/animation/Skinning.cpp
    src/navigation/NavComponents.h
    src/navigation/NavMesh.h
    src/navigation/NavMesh.cpp
    src/navigation/NavMeshRegistry.h
    src/navigation/NavMeshRegistry.cpp
    src/navigation/NavPath.h
    src/navigation/NavPath.cpp
    src/navigation/NavMeshBuilder.h
    src/navigation/NavMeshBuilder.cpp
    src/navigation/NavigationSystem.h
    src/navigation/NavigationSystem.cpp
    src/ui/UIComponents.h
    src/ui/UIIntent.h
    src/ui/RuntimeUISystem.h
    src/ui/RuntimeUISystem.cpp
    src/audio/AudioComponents.h
    src/assets/AssetHandle.h
    src/rendering/Material.h
)

target_include_directories(SuGarCore PUBLIC
    src
    external/glm/glm
    external/glfw/include
)

# Auto-export Core's symbols so the engine and the game DLL can link against it
# without hand-annotating every declaration. Core's shared singletons (e.g.
# BehaviorRegistry's table) then live in exactly one module.
set_target_properties(SuGarCore PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)

# Component access tracking (Phase 13B) -- the ECS reports every storage access so
# the scheduler can enforce each system's declared read/write sets. A development
# guard rail: Debug only, so release builds keep the bare storage ops. PUBLIC
# because ComponentStorage is a header template instantiated in the engine and the
# game DLL too; all three modules must agree on whether tracking is compiled in.
target_compile_definitions(SuGarCore PUBLIC $<$<CONFIG:Debug>:SUGAR_ACCESS_TRACKING>)

# --- Game module (hot-swappable) --------------------------------------------
# Concrete gameplay behaviors, compiled into a DLL that links ONLY against Core.
# The engine loads it at runtime (GameModuleLoader) and calls registerGameBehaviors;
# recompiling it while the engine runs is the basis for code hot reload (12C).
add_library(SuGarGame SHARED
    src/game/GameModule.h
    src/game/GameBehaviors.cpp
)
target_link_libraries(SuGarGame PRIVATE SuGarCore)

add_executable(SuGarEngine
    src/main.cpp
    src/core/SnapshotStorage.h
    src/GameModuleLoader.h
    src/GameModuleLoader.cpp
    src/game/GameModule.h
    src/SuGarApp.cpp
    src/Renderer.cpp
    src/BasicTrianglePass.cpp
    src/assets/ModelLoader.cpp
    src/assets/ModelLoader.h
    src/assets/GltfLoader.cpp
    src/assets/GltfLoader.h
    src/assets/GltfModel.h
    src/assets/ModelImporter.cpp
    src/assets/ModelImporter.h
    src/assets/tiny_gltf_impl.cpp
    src/assets/AssetRegistry.cpp
    src/assets/AssetRegistry.h
    src/assets/FileWatcher.cpp
    src/assets/FileWatcher.h
    src/assets/ResourceManager.cpp
    src/assets/ResourceManager.h
    src/assets/stb_image.cpp
    external/ImGuizmo/ImGuizmo.cpp
    external/ImGuizmo/ImGuizmo.h
    src/editor/EditorCommand.h
    src/editor/EditorCommands.h
    src/editor/EntityQuery.h
    src/editor/ViewportOverlay.h
    src/SelfTests.h
    src/Benchmarks.h
    src/StressTests.h
    src/ui/RmlSystemInterface.h
    src/ui/RmlSystemInterface.cpp
    src/ui/RmlVulkanRenderer.h
    src/ui/RmlVulkanRenderer.cpp
    src/ui/RuntimeUIView.h
    src/ui/RuntimeUIView.cpp
    src/audio/AudioClip.h
    src/audio/AudioEngine.h
    src/audio/AudioEngine.cpp
    src/audio/AudioLoader.h
    src/audio/AudioLoader.cpp
    src/audio/AudioSystem.h
    src/audio/AudioSystem.cpp
    src/audio/miniaudio_impl.cpp
    src/navigation/NavMeshBaker.h
    src/navigation/NavMeshBaker.cpp
    src/physics/PhysicsWorld.cpp
    src/physics/PhysicsWorld.h
    src/scene/DrawList.cpp
    src/scene/DrawList.h
    src/scene/Light.h
    src/scene/SceneSerializer.cpp
    src/scene/SceneSerializer.h
    src/rendering/Camera.h
    src/rendering/Mesh.cpp
    src/rendering/Mesh.h
    src/rendering/Texture.cpp
    src/rendering/Texture.h
    src/rendering/Vertex.h
    src/rendering/UniformBufferObject.h
)

target_link_libraries(SuGarEngine
    SuGarCore
    glfw
    imgui
    RmlUi::Core
    Vulkan::Vulkan
)

# The engine loads SuGarGame.dll at runtime (not linked), so ensure it's built.
add_dependencies(SuGarEngine SuGarGame)

target_include_directories(SuGarEngine PRIVATE
    src
    external/glfw/include
    external/glm
    external/imgui
    external/imgui/backends
    external/stb
    external/tinygltf
    external/miniaudio
    external/ImGuizmo
)

# Compile GLSL shaders (.vert/.frag) into SPIR-V in the build/shaders folder
file(GLOB SHADER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/shaders/*.vert
    ${CMAKE_SOURCE_DIR}/shaders/*.frag
)

set(SHADER_BIN_DIR ${CMAKE_BINARY_DIR}/shaders)
set(SPIRV_FILES)

foreach(SRC ${SHADER_SOURCES})
    # Use the full filename (including .vert/.frag) to avoid collisions
    get_filename_component(FILE_NAME ${SRC} NAME)
    set(OUT ${SHADER_BIN_DIR}/${FILE_NAME}.spv)

    add_custom_command(
        OUTPUT ${OUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SHADER_BIN_DIR}
        COMMAND glslc ${SRC} -o ${OUT}
        DEPENDS ${SRC}
        COMMENT "Compiling shader ${SRC} -> ${OUT}"
        VERBATIM
    )

    list(APPEND SPIRV_FILES ${OUT})
endforeach()

add_custom_target(compile_shaders ALL DEPENDS ${SPIRV_FILES})
add_dependencies(SuGarEngine compile_shaders)

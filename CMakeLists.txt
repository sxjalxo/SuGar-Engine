cmake_minimum_required(VERSION 3.21)
project(SuGarEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)

add_subdirectory(external/glfw)

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

add_executable(SuGarEngine
    src/main.cpp
    src/ecs/Entity.h
    src/ecs/EntityManager.cpp
    src/ecs/EntityManager.h
    src/ecs/ComponentStorage.h
    src/ecs/Components.h
    src/ecs/Registry.h
    src/core/Input.cpp
    src/core/Input.h
    src/core/InputActions.cpp
    src/core/InputActions.h
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
    src/assets/AssetHandle.h
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
    src/audio/AudioClip.h
    src/audio/AudioComponents.h
    src/audio/AudioEngine.h
    src/audio/AudioEngine.cpp
    src/audio/AudioLoader.h
    src/audio/AudioLoader.cpp
    src/audio/AudioSystem.h
    src/audio/AudioSystem.cpp
    src/audio/miniaudio_impl.cpp
    src/physics/CollisionEvent.h
    src/physics/PhysicsComponents.h
    src/physics/PhysicsWorld.cpp
    src/physics/PhysicsWorld.h
    src/scene/Transform.h
    src/scene/TransformMath.h
    src/scene/Behavior.h
    src/scene/BehaviorRegistry.cpp
    src/scene/BehaviorRegistry.h
    src/scene/DrawList.cpp
    src/scene/DrawList.h
    src/scene/GameObject.h
    src/scene/Light.h
    src/scene/Scene.h
    src/scene/SceneSerializer.cpp
    src/scene/SceneSerializer.h
    src/rendering/Camera.h
    src/rendering/Material.h
    src/rendering/Mesh.cpp
    src/rendering/Mesh.h
    src/rendering/Texture.cpp
    src/rendering/Texture.h
    src/rendering/Vertex.h
    src/rendering/UniformBufferObject.h
)

target_link_libraries(SuGarEngine
    glfw
    imgui
    Vulkan::Vulkan
)

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

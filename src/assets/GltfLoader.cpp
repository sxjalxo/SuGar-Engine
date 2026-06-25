#include "assets/GltfLoader.h"
#include "assets/GltfModel.h"
#include "rendering/Mesh.h"
#include "rendering/Vertex.h"

// tinygltf is included here for *parsing only*. The declarations match the flags
// used in tiny_gltf_impl.cpp so no stb image code is pulled in. Nothing of
// tinygltf's types escapes this translation unit.
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include "tiny_gltf.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace {

std::string lowerExtension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return "";
    }
    std::string ext = path.substr(dot);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return ext;
}

void loadTinyModel(const std::string& path, tinygltf::Model& model) {
    tinygltf::TinyGLTF loader;
    std::string error;
    std::string warning;

    const std::string extension = lowerExtension(path);
    const bool loaded = extension == ".glb"
        ? loader.LoadBinaryFromFile(&model, &error, &warning, path)
        : loader.LoadASCIIFromFile(&model, &error, &warning, path);

    if (!loaded) {
        throw std::runtime_error("failed to load glTF '" + path + "': " +
                                 (error.empty() ? "unknown error" : error));
    }
}

// Reads a float-typed accessor (e.g. VEC2/VEC3) into a flat float array.
// Returns the number of components per element (2 or 3), or 0 if missing.
int readFloatAccessor(const tinygltf::Model& model, int accessorIndex, std::vector<float>& out, size_t& count) {
    out.clear();
    count = 0;
    if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size())) {
        return 0;
    }

    const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
    if (accessor.componentType != TINYGLTF_COMPONENT_TYPE_FLOAT || accessor.bufferView < 0) {
        return 0;
    }

    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[view.buffer];
    const int components = tinygltf::GetNumComponentsInType(static_cast<uint32_t>(accessor.type));
    const int stride = accessor.ByteStride(view);
    if (components <= 0 || stride <= 0) {
        return 0;
    }

    const unsigned char* base = buffer.data.data() + view.byteOffset + accessor.byteOffset;
    count = accessor.count;
    out.resize(count * static_cast<size_t>(components));
    for (size_t i = 0; i < count; i++) {
        const unsigned char* element = base + i * static_cast<size_t>(stride);
        for (int c = 0; c < components; c++) {
            float value = 0.0f;
            std::memcpy(&value, element + c * sizeof(float), sizeof(float));
            out[i * static_cast<size_t>(components) + static_cast<size_t>(c)] = value;
        }
    }
    return components;
}

void readIndices(const tinygltf::Model& model, int accessorIndex, uint32_t vertexOffset, std::vector<uint32_t>& out) {
    const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[view.buffer];
    const int stride = accessor.ByteStride(view);
    const unsigned char* base = buffer.data.data() + view.byteOffset + accessor.byteOffset;

    for (size_t i = 0; i < accessor.count; i++) {
        const unsigned char* element = base + i * static_cast<size_t>(stride);
        uint32_t index = 0;
        switch (accessor.componentType) {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT: {
                uint32_t value = 0;
                std::memcpy(&value, element, sizeof(uint32_t));
                index = value;
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: {
                uint16_t value = 0;
                std::memcpy(&value, element, sizeof(uint16_t));
                index = value;
                break;
            }
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                index = *element;
                break;
            default:
                throw std::runtime_error("unsupported glTF index component type.");
        }
        out.push_back(vertexOffset + index);
    }
}

// Appends one primitive's geometry to the engine mesh.
void appendPrimitive(const tinygltf::Model& model, const tinygltf::Primitive& primitive, Mesh& mesh) {
    if (primitive.mode != TINYGLTF_MODE_TRIANGLES && primitive.mode != -1) {
        return; // only triangle lists in v1
    }
    const auto positionIt = primitive.attributes.find("POSITION");
    if (positionIt == primitive.attributes.end()) {
        return;
    }

    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    size_t positionCount = 0;
    size_t normalCount = 0;
    size_t uvCount = 0;

    const int positionComponents = readFloatAccessor(model, positionIt->second, positions, positionCount);
    if (positionComponents < 3 || positionCount == 0) {
        return;
    }

    const auto normalIt = primitive.attributes.find("NORMAL");
    if (normalIt != primitive.attributes.end()) {
        readFloatAccessor(model, normalIt->second, normals, normalCount);
    }
    const auto uvIt = primitive.attributes.find("TEXCOORD_0");
    if (uvIt != primitive.attributes.end()) {
        readFloatAccessor(model, uvIt->second, uvs, uvCount);
    }

    const uint32_t vertexOffset = static_cast<uint32_t>(mesh.vertices.size());

    for (size_t i = 0; i < positionCount; i++) {
        Vertex vertex{};
        vertex.pos[0] = positions[i * 3 + 0];
        vertex.pos[1] = positions[i * 3 + 1];
        vertex.pos[2] = positions[i * 3 + 2];

        if (normalCount == positionCount) {
            vertex.normal[0] = normals[i * 3 + 0];
            vertex.normal[1] = normals[i * 3 + 1];
            vertex.normal[2] = normals[i * 3 + 2];
        } else {
            vertex.normal[0] = 0.0f;
            vertex.normal[1] = 1.0f;
            vertex.normal[2] = 0.0f;
        }

        if (uvCount == positionCount) {
            vertex.uv[0] = uvs[i * 2 + 0];
            vertex.uv[1] = uvs[i * 2 + 1];
        } else {
            vertex.uv[0] = 0.0f;
            vertex.uv[1] = 0.0f;
        }

        mesh.vertices.push_back(vertex);
    }

    if (primitive.indices >= 0) {
        readIndices(model, primitive.indices, vertexOffset, mesh.indices);
    } else {
        for (uint32_t i = 0; i < positionCount; i++) {
            mesh.indices.push_back(vertexOffset + i);
        }
    }
}

void appendMesh(const tinygltf::Model& model, const tinygltf::Mesh& gltfMesh, Mesh& mesh) {
    for (const tinygltf::Primitive& primitive : gltfMesh.primitives) {
        appendPrimitive(model, primitive, mesh);
    }
}

void extractNodeTransform(const tinygltf::Node& node, GltfNodeData& out) {
    if (node.matrix.size() == 16) {
        glm::mat4 matrix(1.0f);
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                matrix[col][row] = static_cast<float>(node.matrix[col * 4 + row]);
            }
        }
        glm::vec3 skew(0.0f);
        glm::vec4 perspective(0.0f);
        glm::quat rotation;
        glm::decompose(matrix, out.scale, rotation, out.position, skew, perspective);
        out.rotation = glm::normalize(rotation);
        return;
    }

    if (node.translation.size() == 3) {
        out.position = glm::vec3(static_cast<float>(node.translation[0]),
                                 static_cast<float>(node.translation[1]),
                                 static_cast<float>(node.translation[2]));
    }
    if (node.scale.size() == 3) {
        out.scale = glm::vec3(static_cast<float>(node.scale[0]),
                              static_cast<float>(node.scale[1]),
                              static_cast<float>(node.scale[2]));
    }
    if (node.rotation.size() == 4) {
        // glTF quaternion order is [x, y, z, w]; glm::quat is (w, x, y, z).
        const glm::quat rotation(static_cast<float>(node.rotation[3]),
                                 static_cast<float>(node.rotation[0]),
                                 static_cast<float>(node.rotation[1]),
                                 static_cast<float>(node.rotation[2]));
        out.rotation = glm::normalize(rotation);
    }
}

} // namespace

void GltfLoader::loadGltf(const std::string& path, Mesh& mesh) {
    tinygltf::Model model;
    loadTinyModel(path, model);

    mesh.vertices.clear();
    mesh.indices.clear();
    for (const tinygltf::Mesh& gltfMesh : model.meshes) {
        appendMesh(model, gltfMesh, mesh);
    }

    if (mesh.vertices.empty()) {
        throw std::runtime_error("glTF '" + path + "' contains no triangle geometry.");
    }
}

void GltfLoader::loadGltfMesh(const std::string& path, int meshIndex, Mesh& mesh) {
    tinygltf::Model model;
    loadTinyModel(path, model);

    if (meshIndex < 0 || meshIndex >= static_cast<int>(model.meshes.size())) {
        throw std::runtime_error("glTF '" + path + "' has no mesh index " + std::to_string(meshIndex));
    }

    mesh.vertices.clear();
    mesh.indices.clear();
    appendMesh(model, model.meshes[meshIndex], mesh);

    if (mesh.vertices.empty()) {
        throw std::runtime_error("glTF '" + path + "' mesh " + std::to_string(meshIndex) + " has no geometry.");
    }
}

void GltfLoader::loadModel(const std::string& path, GltfModelData& outModel) {
    tinygltf::Model model;
    loadTinyModel(path, model);

    outModel = GltfModelData{};

    const std::filesystem::path modelDir = std::filesystem::path(path).parent_path();

    // Materials (PBR factors + base-color texture path).
    outModel.materials.reserve(model.materials.size());
    for (const tinygltf::Material& material : model.materials) {
        GltfMaterialData data;
        data.metallic = static_cast<float>(material.pbrMetallicRoughness.metallicFactor);
        data.roughness = static_cast<float>(material.pbrMetallicRoughness.roughnessFactor);

        const int textureIndex = material.pbrMetallicRoughness.baseColorTexture.index;
        if (textureIndex >= 0 && textureIndex < static_cast<int>(model.textures.size())) {
            const int imageIndex = model.textures[textureIndex].source;
            if (imageIndex >= 0 && imageIndex < static_cast<int>(model.images.size())) {
                const std::string& uri = model.images[imageIndex].uri;
                if (!uri.empty()) {
                    data.baseColorTexture = (modelDir / uri).generic_string();
                }
            }
        }
        outModel.materials.push_back(std::move(data));
    }

    // Per-mesh material (first primitive wins for multi-primitive meshes).
    outModel.meshMaterialIndex.assign(model.meshes.size(), -1);
    for (size_t i = 0; i < model.meshes.size(); i++) {
        if (!model.meshes[i].primitives.empty()) {
            outModel.meshMaterialIndex[i] = model.meshes[i].primitives[0].material;
        }
    }

    // Nodes (transform + mesh ref + children).
    outModel.nodes.resize(model.nodes.size());
    for (size_t i = 0; i < model.nodes.size(); i++) {
        const tinygltf::Node& node = model.nodes[i];
        GltfNodeData data;
        data.name = node.name.empty() ? ("Node " + std::to_string(i)) : node.name;
        extractNodeTransform(node, data);
        data.meshIndex = node.mesh;
        data.children.assign(node.children.begin(), node.children.end());
        outModel.nodes[i] = std::move(data);
    }

    // Roots: from the default scene if present, else nodes that are never a child.
    int sceneIndex = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (sceneIndex < static_cast<int>(model.scenes.size())) {
        for (int nodeIndex : model.scenes[sceneIndex].nodes) {
            outModel.roots.push_back(nodeIndex);
        }
    } else {
        std::vector<bool> isChild(model.nodes.size(), false);
        for (const auto& node : outModel.nodes) {
            for (int child : node.children) {
                if (child >= 0 && child < static_cast<int>(isChild.size())) {
                    isChild[child] = true;
                }
            }
        }
        for (size_t i = 0; i < outModel.nodes.size(); i++) {
            if (!isChild[i]) {
                outModel.roots.push_back(static_cast<int>(i));
            }
        }
    }
}

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
#include <unordered_map>
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

// JOINTS_0 is integer data (unsigned byte or short per the spec), so it cannot go
// through readFloatAccessor — that reads raw bytes as floats and would produce
// garbage rather than fail. Returns the component count (4 for VEC4), or 0.
int readUIntAccessor(const tinygltf::Model& model, int accessorIndex, std::vector<uint32_t>& out, size_t& count) {
    out.clear();
    count = 0;
    if (accessorIndex < 0 || accessorIndex >= static_cast<int>(model.accessors.size())) {
        return 0;
    }

    const tinygltf::Accessor& accessor = model.accessors[accessorIndex];
    if (accessor.bufferView < 0) {
        return 0;
    }
    const tinygltf::BufferView& view = model.bufferViews[accessor.bufferView];
    const tinygltf::Buffer& buffer = model.buffers[view.buffer];
    const int components = tinygltf::GetNumComponentsInType(static_cast<uint32_t>(accessor.type));
    const int stride = accessor.ByteStride(view);
    if (components <= 0 || stride <= 0) {
        return 0;
    }

    size_t componentSize = 0;
    switch (accessor.componentType) {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:  componentSize = 1; break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT: componentSize = 2; break;
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:   componentSize = 4; break;
        default: return 0; // signed/float joint indices are not a thing; refuse it
    }

    const unsigned char* base = buffer.data.data() + view.byteOffset + accessor.byteOffset;
    count = accessor.count;
    out.resize(count * static_cast<size_t>(components));
    for (size_t i = 0; i < count; i++) {
        const unsigned char* element = base + i * static_cast<size_t>(stride);
        for (int c = 0; c < components; c++) {
            uint32_t value = 0;
            std::memcpy(&value, element + c * componentSize, componentSize);
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

    // Skinning influences (Phase 17C.2). Absent on static geometry, which is the
    // common case — the vertices then keep their all-zero defaults, meaning
    // "unskinned", and the mesh is drawn through the ordinary pipeline.
    std::vector<uint32_t> jointIndices;
    std::vector<float> jointWeights;
    size_t jointCount = 0;
    size_t weightCount = 0;

    const auto jointsIt = primitive.attributes.find("JOINTS_0");
    if (jointsIt != primitive.attributes.end()) {
        readUIntAccessor(model, jointsIt->second, jointIndices, jointCount);
    }
    const auto weightsIt = primitive.attributes.find("WEIGHTS_0");
    if (weightsIt != primitive.attributes.end()) {
        readFloatAccessor(model, weightsIt->second, jointWeights, weightCount);
    }
    // Both or neither: a mesh with joints but no weights would skin every vertex by
    // zero and collapse to the origin, which is worse than not skinning it.
    const bool hasSkinning = jointCount == positionCount && weightCount == positionCount;

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

        if (hasSkinning) {
            for (int c = 0; c < 4; c++) {
                // Joint indices are clamped into the byte the vertex format carries.
                // A skin with >255 joints would alias silently otherwise; clamping
                // pins the influence to joint 255 instead of wrapping it to 0, which
                // is at least a *local* deformation rather than the whole limb
                // snapping to the root.
                const uint32_t joint = jointIndices[i * 4 + static_cast<size_t>(c)];
                vertex.joints[c] = static_cast<uint8_t>(joint > 255u ? 255u : joint);
                vertex.weights[c] = jointWeights[i * 4 + static_cast<size_t>(c)];
            }
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

// STEP holds the previous key; everything else is treated as LINEAR. CUBICSPLINE
// is *approximated* rather than evaluated: see readKeyIndex below.
Interpolation parseInterpolation(const std::string& mode) {
    return mode == "STEP" ? Interpolation::Step : Interpolation::Linear;
}

// glTF CUBICSPLINE stores three values per key — [inTangent, value, outTangent] —
// so the real keyframe sits at 3*i + 1. Reading only those and interpolating them
// linearly is exact *at* every keyframe and merely less smooth between them, which
// is a far better failure mode than dropping the channel (silently missing
// animation) or misreading the triples as keys (garbage). Full cubic evaluation
// lands when a real asset needs it — docs/DESIGN_ANIMATION.md, open question 4.
struct KeyLayout {
    size_t stride = 1;
    size_t offset = 0;
};

KeyLayout keyLayout(bool cubic) {
    return cubic ? KeyLayout{ 3, 1 } : KeyLayout{ 1, 0 };
}

bool readTimes(const tinygltf::Model& model, int accessorIndex, std::vector<float>& out) {
    size_t count = 0;
    return readFloatAccessor(model, accessorIndex, out, count) == 1 && count > 0;
}

bool readVec3Keys(const tinygltf::Model& model, int accessorIndex, bool cubic, std::vector<glm::vec3>& out) {
    std::vector<float> raw;
    size_t count = 0;
    if (readFloatAccessor(model, accessorIndex, raw, count) != 3 || count == 0) {
        return false;
    }
    const KeyLayout layout = keyLayout(cubic);
    if (count % layout.stride != 0) {
        return false;
    }
    out.clear();
    for (size_t i = 0; i < count / layout.stride; i++) {
        const size_t element = i * layout.stride + layout.offset;
        out.emplace_back(raw[element * 3 + 0], raw[element * 3 + 1], raw[element * 3 + 2]);
    }
    return true;
}

bool readQuatKeys(const tinygltf::Model& model, int accessorIndex, bool cubic, std::vector<glm::quat>& out) {
    std::vector<float> raw;
    size_t count = 0;
    // Float only. glTF also permits normalized byte/short rotations; those come back
    // as 0 components here and the channel is skipped rather than misread.
    if (readFloatAccessor(model, accessorIndex, raw, count) != 4 || count == 0) {
        return false;
    }
    const KeyLayout layout = keyLayout(cubic);
    if (count % layout.stride != 0) {
        return false;
    }
    out.clear();
    for (size_t i = 0; i < count / layout.stride; i++) {
        const size_t element = i * layout.stride + layout.offset;
        // glTF quaternion order is [x, y, z, w]; glm::quat is (w, x, y, z).
        out.push_back(glm::normalize(glm::quat(raw[element * 4 + 3],
                                               raw[element * 4 + 0],
                                               raw[element * 4 + 1],
                                               raw[element * 4 + 2])));
    }
    return true;
}

// glTF models an animation as (channel -> sampler) pairs, each targeting one node
// and one property. SuGar wants one TransformTrack per animated node, so channels
// are grouped by target node here — the shape conversion this boundary exists for.
// Node *indices* are resolved to node *names* on the way out, so nothing downstream
// depends on glTF's numbering.
void extractAnimations(const tinygltf::Model& model,
                       const std::vector<GltfNodeData>& nodes,
                       std::vector<AnimationClip>& out) {
    out.clear();
    out.reserve(model.animations.size());

    for (size_t a = 0; a < model.animations.size(); a++) {
        const tinygltf::Animation& animation = model.animations[a];

        AnimationClip clip;
        clip.name = animation.name.empty() ? ("Animation " + std::to_string(a)) : animation.name;

        // Tracks are appended in channel order (deterministic); the map only avoids
        // emitting three single-channel tracks for one node.
        std::unordered_map<int, size_t> trackByNode;
        const auto trackFor = [&](int node) -> TransformTrack& {
            const auto it = trackByNode.find(node);
            if (it != trackByNode.end()) {
                return clip.tracks[it->second];
            }
            TransformTrack track;
            track.target = nodes[static_cast<size_t>(node)].name;
            clip.tracks.push_back(std::move(track));
            trackByNode.emplace(node, clip.tracks.size() - 1);
            return clip.tracks.back();
        };

        for (const tinygltf::AnimationChannel& channel : animation.channels) {
            if (channel.target_node < 0 || channel.target_node >= static_cast<int>(nodes.size())) {
                continue;
            }
            if (channel.sampler < 0 || channel.sampler >= static_cast<int>(animation.samplers.size())) {
                continue;
            }
            const tinygltf::AnimationSampler& sampler = animation.samplers[channel.sampler];

            std::vector<float> times;
            if (!readTimes(model, sampler.input, times)) {
                continue;
            }
            const bool cubic = sampler.interpolation == "CUBICSPLINE";
            const Interpolation interpolation = parseInterpolation(sampler.interpolation);

            // Each branch reads its values *before* touching trackFor, so a channel
            // that fails to decode never leaves an empty track behind.
            if (channel.target_path == "translation") {
                std::vector<glm::vec3> values;
                if (!readVec3Keys(model, sampler.output, cubic, values) || values.size() != times.size()) {
                    continue;
                }
                TransformTrack& track = trackFor(channel.target_node);
                track.translation.times = times;
                track.translation.values = std::move(values);
                track.translation.interpolation = interpolation;
            } else if (channel.target_path == "rotation") {
                std::vector<glm::quat> values;
                if (!readQuatKeys(model, sampler.output, cubic, values) || values.size() != times.size()) {
                    continue;
                }
                TransformTrack& track = trackFor(channel.target_node);
                track.rotation.times = times;
                track.rotation.values = std::move(values);
                track.rotation.interpolation = interpolation;
            } else if (channel.target_path == "scale") {
                std::vector<glm::vec3> values;
                if (!readVec3Keys(model, sampler.output, cubic, values) || values.size() != times.size()) {
                    continue;
                }
                TransformTrack& track = trackFor(channel.target_node);
                track.scale.times = times;
                track.scale.values = std::move(values);
                track.scale.interpolation = interpolation;
            }
            // "weights" (morph targets) has no home in the transform model; skipped.
        }

        if (clip.tracks.empty()) {
            continue; // nothing this engine can play (e.g. morph-target-only)
        }
        clip.duration = computeDuration(clip.tracks);
        out.push_back(std::move(clip));
    }
}

// glTF gives a skin its joints as node indices plus an accessor of inverse bind
// matrices. Same boundary conversion as animations: indices become names, so the
// engine never depends on glTF's node numbering. Joint *order* is preserved
// exactly — JOINTS_0 vertex attributes index into it.
void extractSkins(const tinygltf::Model& model,
                  const std::vector<GltfNodeData>& nodes,
                  std::vector<Skin>& out) {
    out.clear();
    out.reserve(model.skins.size());

    for (size_t s = 0; s < model.skins.size(); s++) {
        const tinygltf::Skin& gltfSkin = model.skins[s];

        Skin skin;
        skin.name = gltfSkin.name.empty() ? ("Skin " + std::to_string(s)) : gltfSkin.name;

        for (int joint : gltfSkin.joints) {
            if (joint < 0 || joint >= static_cast<int>(nodes.size())) {
                skin.joints.clear(); // a hole would silently shift every later joint
                break;
            }
            skin.joints.push_back(nodes[static_cast<size_t>(joint)].name);
        }
        if (skin.joints.empty()) {
            continue;
        }

        // Inverse bind matrices: one mat4 per joint, column-major in glTF exactly as
        // in glm, so the floats copy straight across.
        std::vector<float> raw;
        size_t count = 0;
        if (readFloatAccessor(model, gltfSkin.inverseBindMatrices, raw, count) != 16 ||
            count != skin.joints.size()) {
            // Spec allows the accessor to be omitted, meaning identity for every
            // joint (the mesh is already in bind space).
            skin.inverseBindMatrices.assign(skin.joints.size(), glm::mat4(1.0f));
        } else {
            skin.inverseBindMatrices.reserve(count);
            for (size_t i = 0; i < count; i++) {
                glm::mat4 matrix(1.0f);
                std::memcpy(&matrix[0][0], raw.data() + i * 16, sizeof(float) * 16);
                skin.inverseBindMatrices.push_back(matrix);
            }
        }

        out.push_back(std::move(skin));
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
        data.skinIndex = node.skin;
        data.children.assign(node.children.begin(), node.children.end());
        outModel.nodes[i] = std::move(data);
    }

    // Animations and skins, resolved against the node names built above.
    extractAnimations(model, outModel.nodes, outModel.animations);
    extractSkins(model, outModel.nodes, outModel.skins);

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

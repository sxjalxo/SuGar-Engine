#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>

// Phase 17C.2 — skinning influences (`joints` / `weights`) live on the *one* Vertex
// type rather than in a separate skinned vertex format.
//
// The honest cost: +20 bytes on every vertex, including static geometry that will
// never be skinned (32 -> 52 bytes, ~60% more vertex memory). Bought deliberately:
// one Vertex means one Mesh, one loader path, one ResourceManager entry, and one
// buffer — a second vertex format would fork all of them, and forked asset paths
// are the kind of complexity that stays forever (Rule 8). Skinned and unskinned
// share the buffer *stride* and differ only in which attributes their pipeline
// declares, so nothing else in the renderer forks either.
//
// This is the first thing to revisit if vertex memory ever shows up in a
// measurement (Rule 18) — likely as unorm8 weights (+8 B instead of +20) before a
// split format. Nothing has measured it yet, so it stays simple.
struct Vertex
{
    float pos[3];
    float normal[3];
    float uv[2];

    // glTF JOINTS_0 / WEIGHTS_0: up to four influences per vertex. Joint values
    // index the skin's joint order (see Skin::joints); a weight of 0 makes the
    // corresponding joint index irrelevant. All-zero here means "unskinned", which
    // is the correct default for every mesh that never had the attributes.
    // JOINTS_1 (5-8 influences) is not read; four covers ordinary characters.
    uint8_t joints[4] = {0, 0, 0, 0};
    float weights[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    static VkVertexInputBindingDescription getBindingDescription()
    {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(Vertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }

    // Static geometry: position/normal/uv only. The skinning attributes are present
    // in the buffer but simply not declared, which is what lets both pipelines share
    // one stride.
    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 3> attributes{};

        attributes[0].binding = 0;
        attributes[0].location = 0;
        attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[0].offset = offsetof(Vertex, pos);

        attributes[1].binding = 0;
        attributes[1].location = 1;
        attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributes[1].offset = offsetof(Vertex, normal);

        attributes[2].binding = 0;
        attributes[2].location = 2;
        attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributes[2].offset = offsetof(Vertex, uv);

        return attributes;
    }

    // Skinned geometry: the same three, plus the influences. R8G8B8A8_UINT reaches
    // the shader as a uvec4 of joint indices (not normalized — these are indices,
    // and normalizing them would silently turn joint 255 into 1.0).
    static std::array<VkVertexInputAttributeDescription, 5> getSkinnedAttributeDescriptions()
    {
        std::array<VkVertexInputAttributeDescription, 5> attributes{};
        const auto base = getAttributeDescriptions();
        attributes[0] = base[0];
        attributes[1] = base[1];
        attributes[2] = base[2];

        attributes[3].binding = 0;
        attributes[3].location = 3;
        attributes[3].format = VK_FORMAT_R8G8B8A8_UINT;
        attributes[3].offset = offsetof(Vertex, joints);

        attributes[4].binding = 0;
        attributes[4].location = 4;
        attributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        attributes[4].offset = offsetof(Vertex, weights);

        return attributes;
    }
};

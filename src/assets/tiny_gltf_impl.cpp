// Compiles the tinygltf implementation in one isolated translation unit.
// Image decoding is disabled: the engine never asks tinygltf to decode textures
// (that avoids a clash with our own stb_image.cpp and keeps tinygltf parse-only).
// tinygltf is confined to the loader layer and must not leak into the engine.
#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_STB_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE
#define TINYGLTF_NO_INCLUDE_STB_IMAGE_WRITE
#include "tiny_gltf.h"

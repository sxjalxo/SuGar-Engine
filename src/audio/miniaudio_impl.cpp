// Compiles the miniaudio implementation in one isolated translation unit, so the
// (very large) single-header implementation only recompiles when the vendored
// header changes — not whenever AudioEngine.cpp does. Mirrors stb_image.cpp and
// tiny_gltf_impl.cpp. miniaudio is confined to the audio layer.
//
// We use miniaudio only as the device backend + file decoder; encoding and
// waveform generation are disabled (the engine never needs them).
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

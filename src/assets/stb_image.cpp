// Single translation unit that compiles the stb_image implementation.
// Keeping it isolated means the (large) implementation only recompiles when the
// vendored header changes, not whenever ResourceManager.cpp does.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

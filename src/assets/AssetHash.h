#pragma once

#include <cstdint>
#include <string>

// Content hashing for cook staleness (docs/DESIGN_ASSET_PIPELINE.md).
//
// Content hash, not mtime: mtime says "a write happened", the hash says "the bytes
// differ". Touching a file, switching git branches back and forth, or a fresh checkout
// must not recook.
//
// 64-bit FNV-1a: dependency-free, deterministic, and adequate for change detection --
// this is not an adversarial integrity check. The algorithm is part of the cooker
// version (see CookerVersion below): swapping it changes every key, so it must
// invalidate the cache.
namespace AssetHash {

// Bump on ANY change to a cooked format, to what is hashed, or to how it is hashed.
// A single integer folded into every cook key is what makes "delete the cache" never a
// step a developer has to know about.
constexpr uint32_t CookerVersion = 1;

uint64_t hashBytes(const void* data, size_t size);
uint64_t hashString(const std::string& value);

// Combines an existing hash with more bytes -- for building a key out of several
// inputs (source bytes, .meta bytes, cooker version) in a fixed, documented order.
uint64_t hashCombine(uint64_t seed, const void* data, size_t size);
uint64_t hashCombineString(uint64_t seed, const std::string& value);

// Hashes a file's contents. Returns false (and leaves `out` untouched) if the file
// cannot be read -- a missing source is a reportable state, not an exception (Rule 13).
bool hashFile(const std::string& path, uint64_t& out);

// Lowercase 16-digit hex. The on-disk spelling of a hash: stable, sortable, and
// locale-independent (no stream formatting, no std::locale).
std::string toHex(uint64_t hash);

} // namespace AssetHash

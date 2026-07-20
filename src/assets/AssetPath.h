#pragma once

#include <string>

// The asset identity function (docs/DESIGN_ASSET_PIPELINE.md).
//
// A component stores an asset *key*, never a pointer and never a GUID (Rule 21a), so
// this normalization IS the identity: two strings that normalize the same name the same
// asset, forever. Changing the algorithm migrates every scene, prefab and save file on
// disk, which is why it lives in exactly one place instead of being re-derived per
// subsystem. Core-side and headless — the cooker, the database and the editor must all
// agree, and none of them may need a Vulkan device to ask.
namespace AssetPath {

// Normalizes a raw path into an asset key:
//   - splits at the first '#'; the sub-selector is copied VERBATIM (a glTF mesh index
//     or a clip/skin name -- names are case-sensitive identity, not a path)
//   - '\' becomes '/', repeated '/' collapse
//   - "." segments drop, "a/b/../c" becomes "a/c"
//   - anchors at the first "assets/" segment when present, so an absolute path and a
//     relative one produce the same key
//   - ASCII-lowercases the path part (Windows/macOS are case-insensitive; a
//     case-sensitive key would mean one asset there and two on Linux)
//   - no Unicode normalization, no symlink resolution: bytes pass through, and the key
//     is the path the developer typed rather than wherever the filesystem points it
//
// Returns an empty string if the input is invalid: empty, or a ".." that escapes the
// project root. Callers treat empty as "not an asset key" (Rule 13 — ask, don't guess).
std::string normalize(const std::string& rawPath);

// The path part of a key, with any "#sub" removed. Normalized.
std::string pathOf(const std::string& rawPath);

// The "#sub" selector of a key without the '#', verbatim and un-normalized, or "" when
// the key has none.
std::string subOf(const std::string& rawPath);

// Joins a normalized path and a sub-selector back into a key. join(pathOf(k), subOf(k))
// == normalize(k) for every valid k -- the round-trip the self-test asserts.
std::string join(const std::string& normalizedPath, const std::string& sub);

// True if every byte is printable ASCII. Non-ASCII asset names are unsupported: the
// database reports them rather than minting a key that resolves on one machine and not
// another (composition forms, codepage-dependent filesystems).
bool isSupportedAscii(const std::string& value);

} // namespace AssetPath

#include "assets/AssetHash.h"

#include <fstream>
#include <vector>

namespace {
constexpr uint64_t FnvOffsetBasis = 1469598103934665603ULL;
constexpr uint64_t FnvPrime = 1099511628211ULL;
constexpr size_t ReadChunkBytes = 64 * 1024;
} // namespace

uint64_t AssetHash::hashCombine(uint64_t seed, const void* data, size_t size) {
    const unsigned char* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; i++) {
        seed ^= static_cast<uint64_t>(bytes[i]);
        seed *= FnvPrime;
    }
    return seed;
}

uint64_t AssetHash::hashCombineString(uint64_t seed, const std::string& value) {
    return hashCombine(seed, value.data(), value.size());
}

uint64_t AssetHash::hashBytes(const void* data, size_t size) {
    return hashCombine(FnvOffsetBasis, data, size);
}

uint64_t AssetHash::hashString(const std::string& value) {
    return hashBytes(value.data(), value.size());
}

bool AssetHash::hashFile(const std::string& path, uint64_t& out) {
    // Binary, chunked: text mode would translate line endings, which would make the
    // same file hash differently on Windows and Linux -- exactly the machine dependence
    // the design record forbids.
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    uint64_t hash = FnvOffsetBasis;
    std::vector<char> buffer(ReadChunkBytes);
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize read = file.gcount();
        if (read > 0) {
            hash = hashCombine(hash, buffer.data(), static_cast<size_t>(read));
        }
    }

    if (file.bad()) {
        return false;
    }

    out = hash;
    return true;
}

std::string AssetHash::toHex(uint64_t hash) {
    static const char digits[] = "0123456789abcdef";
    std::string hex(16, '0');
    for (int i = 15; i >= 0; i--) {
        hex[static_cast<size_t>(i)] = digits[hash & 0xF];
        hash >>= 4;
    }
    return hex;
}

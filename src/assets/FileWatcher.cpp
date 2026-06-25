#include "assets/FileWatcher.h"
#include <algorithm>
#include <system_error>

namespace {
std::string normalizePath(const std::filesystem::path& path) {
    std::error_code errorCode;
    const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(path, errorCode);
    return (errorCode ? path : canonicalPath).generic_string();
}
} // namespace

void FileWatcher::watch(const std::string& path) {
    const std::filesystem::path rootPath = std::filesystem::path(path);
    const std::filesystem::path normalizedRoot = std::filesystem::weakly_canonical(rootPath);

    if (std::find(roots.begin(), roots.end(), normalizedRoot) == roots.end()) {
        roots.push_back(normalizedRoot);
    }

    scanRoot(normalizedRoot, nullptr);
}

std::vector<std::string> FileWatcher::pollChanges() {
    std::vector<std::string> changedFiles;

    for (const auto& root : roots) {
        scanRoot(root, &changedFiles);
    }

    return changedFiles;
}

void FileWatcher::markDirty(const std::string& path) {
    files[normalizePath(path)] = std::filesystem::file_time_type::min();
}

void FileWatcher::scanRoot(const std::filesystem::path& root, std::vector<std::string>* changedFiles) {
    std::error_code errorCode;
    std::filesystem::recursive_directory_iterator iterator(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        errorCode
    );

    for (const auto& entry : iterator) {
        if (errorCode || !entry.is_regular_file()) {
            continue;
        }

        const std::string normalizedPath = normalizePath(entry.path());
        const auto lastWriteTime = entry.last_write_time(errorCode);
        if (errorCode) {
            continue;
        }

        const auto existing = files.find(normalizedPath);
        if (existing == files.end()) {
            files.emplace(normalizedPath, lastWriteTime);
            if (changedFiles != nullptr) {
                changedFiles->push_back(normalizedPath);
            }
            continue;
        }

        if (existing->second != lastWriteTime) {
            existing->second = lastWriteTime;
            if (changedFiles != nullptr) {
                changedFiles->push_back(normalizedPath);
            }
        }
    }
}

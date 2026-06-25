#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

class FileWatcher {
public:
    void watch(const std::string& path);
    std::vector<std::string> pollChanges();
    void markDirty(const std::string& path);

private:
    void scanRoot(const std::filesystem::path& root, std::vector<std::string>* changedFiles);

    std::vector<std::filesystem::path> roots;
    std::unordered_map<std::string, std::filesystem::file_time_type> files;
};

#pragma once

#include <string>
#include <vector>

struct AssetInfo {
    std::string path;
    std::string name;
    std::string extension;
};

class AssetRegistry {
public:
    void scan(const std::string& root);

    const std::vector<AssetInfo>& getAssets() const { return assets; }

private:
    std::vector<AssetInfo> assets;
};

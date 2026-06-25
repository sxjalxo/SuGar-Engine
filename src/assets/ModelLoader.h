#pragma once

#include <string>

class Mesh;

class ModelLoader {
public:
    static void loadObj(const std::string& path, Mesh& mesh);
};

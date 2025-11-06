#pragma once
#include "../Renderer/Model.h"
#include <string>
#include <memory>
#include <unordered_map>

class FBXLoader;

class ResourceManager {
public:
    static ResourceManager* Get();
    Model* LoadModel(const std::string& filePath);

private:
    ResourceManager();
    static std::unique_ptr<ResourceManager> s_Instance;

    std::unique_ptr<FBXLoader> m_Loader;
    std::unordered_map<std::string, std::unique_ptr<Model>> m_ModelCache;
};
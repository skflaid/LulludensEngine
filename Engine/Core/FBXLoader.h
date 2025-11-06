#pragma once
#include <Renderer/Model.h>
#include <string>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

class FBXLoader {
public:
    Model* Load(const std::string& filePath);
private:
    void ProcessNode(aiNode* node, const aiScene* scene, Model* outModel);
    Mesh* ProcessMesh(aiMesh* mesh, const aiScene* scene, Model* outModel);
    void ProcessMaterials(const aiScene* scene, Model* outModel);
    std::string m_Directory;
};
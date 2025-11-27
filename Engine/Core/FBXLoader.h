#pragma once
#include <Renderer/Model.h>
#include <string>
#include <vector>
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

    // --- 추가: 스켈레탈 데이터 처리 ---
    void ExtractBoneWeights(aiMesh* mesh, Model* outModel, std::vector<ModelVertex>& vertices);
    void BuildSkeletonHierarchy(const aiScene* scene, Model* outModel);
    void ProcessAnimations(const aiScene* scene, Model* outModel);

    std::string m_Directory;
};
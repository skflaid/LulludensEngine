#include "FBXLoader.h"
#include "Renderer/Model.h" // Model, Mesh, Vertex, Material 구조체
//#include "Core/GameEngine.h"  // 엔진의 핵심 기능 접근용 (가상의 클래스)

// Assimp 헤더
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// DirectX 헤더
#include <DirectXMath.h>
#include <d3d12.h>
#include <wrl.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

Model* FBXLoader::Load(const std::string& filePath) {
    Assimp::Importer importer;

    // aiProcess_ConvertToLeftHanded: DirectX는 왼손 좌표계를 사용하므로 변환
    const aiScene* scene = importer.ReadFile(filePath,
        aiProcess_Triangulate |
        aiProcess_ConvertToLeftHanded |
        aiProcess_FlipUVs |
        aiProcess_CalcTangentSpace);

    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        // Log::Error("ASSIMP:: %s", importer.GetErrorString());
        return nullptr;
    }

    // 디렉토리 경로 추출 (Windows와 Unix 모두 지원)
    size_t lastSlash = filePath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        m_Directory = filePath.substr(0, lastSlash);
    }
    else {
        m_Directory = ".";
    }

    auto model = std::make_unique<Model>();

    ProcessMaterials(scene, model.get());
    ProcessNode(scene->mRootNode, scene, model.get());

    return model.release();
}

void FBXLoader::ProcessNode(aiNode* node, const aiScene* scene, Model* outModel) {
    // 현재 노드에 속한 모든 메시를 처리
    for (unsigned int i = 0; i < node->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        outModel->Meshes.push_back(std::unique_ptr<Mesh>(ProcessMesh(mesh, scene, outModel)));
    }
    // 자식 노드들을 재귀적으로 방문
    for (unsigned int i = 0; i < node->mNumChildren; i++) {
        ProcessNode(node->mChildren[i], scene, outModel);
    }
}

Mesh* FBXLoader::ProcessMesh(aiMesh* mesh, const aiScene* scene, Model* outModel) {
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;

    // 1. 정점 데이터(Vertex Data)를 순회하며 추출
    for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
        ModelVertex v;
        v.Pos = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };

        if (mesh->HasNormals()) {
            v.Normal = { mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z };
        }
        else {
            v.Normal = { 0.0f, 1.0f, 0.0f }; // 법선이 없을 경우 임시 값
        }

        // Assimp는 여러 개의 텍스처 좌표 채널을 지원. 보통 첫 번째(0) 채널을 사용.
        if (mesh->mTextureCoords[0]) {
            v.TexC = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
        }
        else {
            v.TexC = { 0.0f, 0.0f }; // 텍스처 좌표가 없을 경우
        }
        vertices.push_back(v);
    }

    // 2. 인덱스 데이터(Index Data)를 순회하며 추출
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        // aiProcess_Triangulate 플래그를 사용했으므로, 모든 면(face)은 삼각형임.
        for (unsigned int j = 0; j < face.mNumIndices; j++) {
            indices.push_back(face.mIndices[j]);
        }
    }

    // 3. 추출한 데이터로 엔진의 Mesh 객체 생성
    Mesh* newMesh = new Mesh();
    newMesh->Name = mesh->mName.C_Str();
    newMesh->IndexCount = (UINT)indices.size();
    newMesh->MatIndex = mesh->mMaterialIndex;
    
    // CPU 데이터 저장 (Entity 변환 시 사용)
    newMesh->Vertices = std::move(vertices);
    newMesh->Indices = std::move(indices);

    // TODO: 아래 부분은 엔진의 D3D12 리소스 관리 시스템과 연동해야 합니다.
    // -- 버퍼 생성 코드 (D3D12 리소스 생성 함수 사용) --
    // 이 함수들은 CPU의 데이터를 GPU 메모리로 복사하는 역할을 합니다.
    // GPU 버퍼는 Entity로 변환할 때 RenderSystem에서 생성됩니다.
    //
    // const UINT vbByteSize = (UINT)newMesh->Vertices.size() * sizeof(ModelVertex);
    // newMesh->VertexBuffer = GameEngine::GetRenderer()->CreateVertexBuffer(newMesh->Vertices.data(), vbByteSize);
    //
    // const UINT ibByteSize = (UINT)newMesh->Indices.size() * sizeof(uint32_t);
    // newMesh->IndexBuffer = GameEngine::GetRenderer()->CreateIndexBuffer(newMesh->Indices.data(), ibByteSize);
    //
    // newMesh->VertexBufferView = newMesh->VertexBuffer->GetVertexBufferView();
    // newMesh->IndexBufferView = newMesh->IndexBuffer->GetIndexBufferView();

    return newMesh;
}

void FBXLoader::ProcessMaterials(const aiScene* scene, Model* outModel) {
    outModel->Materials.resize(scene->mNumMaterials);
    for (unsigned int i = 0; i < scene->mNumMaterials; ++i) {
        aiMaterial* mat = scene->mMaterials[i];
        auto material = std::make_unique<Material>();

        material->Name = mat->GetName().C_Str();

        // Diffuse 텍스처 경로를 가져옴
        aiString texPath;
        if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath) == AI_SUCCESS) {
            std::string fullTexPath = m_Directory + "/" + texPath.C_Str();

            // TODO: 아래는 텍스처를 로드하고 SRV를 생성하는 부분입니다.
            // 이 로직은 텍스처 리소스를 관리하는 별도의 'TextureManager'에서 처리하는 것이 이상적입니다.
            //
            // int srvIndex = GameEngine::GetTextureManager()->LoadTexture(fullTexPath);
            // material->DiffuseSrvHeapIndex = srvIndex;
        }

        outModel->Materials[i] = std::move(material);
    }
}

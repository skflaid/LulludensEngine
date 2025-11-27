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

#include <functional>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{
    DirectX::XMFLOAT4X4 ToXMFLOAT4X4(const aiMatrix4x4& m)
    {
        return DirectX::XMFLOAT4X4(
            (float)m.a1, (float)m.b1, (float)m.c1, (float)m.d1,
            (float)m.a2, (float)m.b2, (float)m.c2, (float)m.d2,
            (float)m.a3, (float)m.b3, (float)m.c3, (float)m.d3,
            (float)m.a4, (float)m.b4, (float)m.c4, (float)m.d4
        );
    }

    int GetOrCreateBoneIndex(Model* model, const std::string& name, const aiBone* bone)
    {
        auto it = model->BoneNameToIndex.find(name);
        if (it != model->BoneNameToIndex.end())
            return it->second;

        ModelBone newBone;
        newBone.Name = name;
        newBone.ParentIndex = -1;
        newBone.Offset = ToXMFLOAT4X4(bone->mOffsetMatrix);

        // Bind pose = inverse of Offset
        XMMATRIX offsetM = XMLoadFloat4x4(&newBone.Offset);
        XMMATRIX bindM = XMMatrixInverse(nullptr, offsetM);
        XMStoreFloat4x4(&newBone.BindTransform, bindM);

        int index = static_cast<int>(model->Bones.size());
        model->Bones.push_back(newBone);
        model->BoneNameToIndex[name] = index;
        return index;
    }
}

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

    // 메시들을 전부 훑은 뒤에 본 계층 + 애니메이션 처리
    BuildSkeletonHierarchy(scene, model.get());
    ProcessAnimations(scene, model.get());

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

    // --- 추가: 스켈레탈 본 웨이트 추출 ---
    ExtractBoneWeights(mesh, outModel, vertices);

    // 2. 인덱스 데이터(Index Data)를 순회하며 추출
    for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
        aiFace face = mesh->mFaces[i];
        // aiProcess_Triangulate 플래그를 사용했으므로, 폴리곤은 삼각형으로 변환됨.
        // 하지만 라인이나 포인트가 포함될 수 있으므로 3개인 경우만 처리.
        if (face.mNumIndices != 3) continue;

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
        auto material = std::make_unique<ModelMaterial>();

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

void FBXLoader::ExtractBoneWeights(aiMesh* mesh, Model* outModel, std::vector<ModelVertex>& vertices)
{
    if (!mesh->HasBones())
        return;

    // 초기화 (혹시 모를 쓰레기값 방지)
    for (auto& v : vertices) {
        for (int i = 0; i < 4; ++i) {
            v.BoneIndices[i] = 0;
            v.BoneWeights[i] = 0.0f;
        }
    }

    // aiMesh::mBones 를 돌면서 각 정점에 본 인덱스/웨이트 할당
    for (unsigned int i = 0; i < mesh->mNumBones; ++i) {
        aiBone* aiBonePtr = mesh->mBones[i];
        std::string boneName = aiBonePtr->mName.C_Str();

        int boneIndex = GetOrCreateBoneIndex(outModel, boneName, aiBonePtr);

        for (unsigned int j = 0; j < aiBonePtr->mNumWeights; ++j) {
            const aiVertexWeight& vw = aiBonePtr->mWeights[j];
            unsigned int vertexId = vw.mVertexId;
            float weight = vw.mWeight;

            if (vertexId >= vertices.size())
                continue;

            auto& v = vertices[vertexId];

            // 4개 슬롯 중 빈 자리 또는 가장 작은 웨이트를 교체
            int slot = -1;
            float minWeight = weight;
            int minIndex = 0;

            for (int k = 0; k < 4; ++k) {
                if (v.BoneWeights[k] == 0.0f) {
                    slot = k;
                    break;
                }
                if (v.BoneWeights[k] < minWeight) {
                    minWeight = v.BoneWeights[k];
                    minIndex = k;
                }
            }

            if (slot == -1) {
                // 이미 4개 꽉 찼으면 가장 작은 웨이트를 교체
                slot = minIndex;
            }

            v.BoneWeights[slot] = weight;
            v.BoneIndices[slot] = static_cast<uint32_t>(boneIndex);
        }
    }

    // 정규화 (총합이 1이 되도록)
    for (auto& v : vertices) {
        float sum =
            v.BoneWeights[0] + v.BoneWeights[1] +
            v.BoneWeights[2] + v.BoneWeights[3];

        if (sum > 0.0f) {
            float inv = 1.0f / sum;
            for (int i = 0; i < 4; ++i) {
                v.BoneWeights[i] *= inv;
            }
        }
    }
}

void FBXLoader::BuildSkeletonHierarchy(const aiScene* scene, Model* outModel)
{
    if (!scene || !scene->mRootNode || outModel->Bones.empty())
        return;

    std::function<void(aiNode*, int)> recurse =
        [&](aiNode* node, int parentBoneIndex)
        {
            std::string nodeName = node->mName.C_Str();
            int currentBoneIndex = parentBoneIndex;

            auto it = outModel->BoneNameToIndex.find(nodeName);
            if (it != outModel->BoneNameToIndex.end()) {
                currentBoneIndex = it->second;
                outModel->Bones[currentBoneIndex].ParentIndex = parentBoneIndex;
            }

            for (unsigned int i = 0; i < node->mNumChildren; ++i) {
                recurse(node->mChildren[i], currentBoneIndex);
            }
        };

    recurse(scene->mRootNode, -1);
}

void FBXLoader::ProcessAnimations(const aiScene* scene, Model* outModel)
{
    if (!scene || !scene->HasAnimations() || outModel->Bones.empty())
        return;

    for (unsigned int a = 0; a < scene->mNumAnimations; ++a) {
        aiAnimation* anim = scene->mAnimations[a];
        ModelAnimationClip clip;

        clip.Name = anim->mName.C_Str();
        if (clip.Name.empty()) {
            clip.Name = "Anim" + std::to_string(a);
        }

        clip.Duration = anim->mDuration;
        clip.TicksPerSecond = (anim->mTicksPerSecond != 0.0)
            ? anim->mTicksPerSecond
            : 25.0; // 기본값

        clip.BoneAnimations.clear();
        clip.BoneAnimations.resize(outModel->Bones.size());

        for (unsigned int c = 0; c < anim->mNumChannels; ++c) {
            aiNodeAnim* channel = anim->mChannels[c];
            std::string channelName = channel->mNodeName.C_Str();

            int boneIndex = outModel->GetBoneIndex(channelName);
            if (boneIndex < 0) continue;

            ModelBoneAnimation& boneAnim = clip.BoneAnimations[boneIndex];

            // Position keys
            for (unsigned int i = 0; i < channel->mNumPositionKeys; ++i) {
                const aiVectorKey& key = channel->mPositionKeys[i];
                ModelKeyframeVec3 kf;
                kf.Time = key.mTime;
                kf.Value = DirectX::XMFLOAT3(
                    (float)key.mValue.x,
                    (float)key.mValue.y,
                    (float)key.mValue.z);
                boneAnim.Translations.push_back(kf);
            }

            // Rotation keys
            for (unsigned int i = 0; i < channel->mNumRotationKeys; ++i) {
                const aiQuatKey& key = channel->mRotationKeys[i];
                ModelKeyframeQuat kf;
                kf.Time = key.mTime;
                kf.Value = DirectX::XMFLOAT4(
                    (float)key.mValue.x,
                    (float)key.mValue.y,
                    (float)key.mValue.z,
                    (float)key.mValue.w);
                boneAnim.Rotations.push_back(kf);
            }

            // Scale keys
            for (unsigned int i = 0; i < channel->mNumScalingKeys; ++i) {
                const aiVectorKey& key = channel->mScalingKeys[i];
                ModelKeyframeVec3 kf;
                kf.Time = key.mTime;
                kf.Value = DirectX::XMFLOAT3(
                    (float)key.mValue.x,
                    (float)key.mValue.y,
                    (float)key.mValue.z);
                boneAnim.Scales.push_back(kf);
            }
        }

        outModel->Animations.push_back(std::move(clip));
    }
}

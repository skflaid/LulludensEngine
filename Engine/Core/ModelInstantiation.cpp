#include "ModelInstantiation.h"
#include "Core/Entity.h"
#include "Renderer/Components/MeshComponent.h"
#include "Renderer/Components/MaterialComponent.h"
#include "Renderer/Components/SkeletonComponent.h"
#include "Renderer/Components/SkeletalAnimationComponent.h"

#include "Renderer/Model.h"

// 어디든 접근 쉬운 cpp에 정적 함수로 두면 편함 (예: ModelInstantiation.cpp 상단에)
static void RebuildBindLocal(class SkeletonComponent* skeleton)
{
    using namespace DirectX;

    const size_t n = skeleton->Bones.size();
    if (n == 0) return;

    std::vector<XMFLOAT4X4> globalBind(n);

    // 1) GlobalBind = inverse(Offset)
    for (size_t i = 0; i < n; ++i) {
        XMMATRIX offset = XMLoadFloat4x4(&skeleton->Bones[i].Offset); // Offset = inverse(bind)
        XMMATRIX gBind = XMMatrixInverse(nullptr, offset);
        XMStoreFloat4x4(&globalBind[i], gBind);
    }

    // 2) BindLocal = inv(GlobalBind[parent]) * GlobalBind[i]
    for (size_t i = 0; i < n; ++i) {
        int p = skeleton->Bones[i].ParentIndex;
        XMMATRIX g = XMLoadFloat4x4(&globalBind[i]);
        XMMATRIX loc;
        if (p >= 0) {
            XMMATRIX gp = XMLoadFloat4x4(&globalBind[(size_t)p]);
            XMMATRIX invGp = XMMatrixInverse(nullptr, gp);
            loc = invGp * g;
        }
        else {
            loc = g; // 루트는 글로벌=로컬
        }
        // 이제 BindTransform에는 "로컬 바인드"를 저장
        XMStoreFloat4x4(&skeleton->Bones[i].BindTransform, loc);
    }
}


namespace ModelInstantiation {
    bool InstantiateToEntity(const Model* model, Entity* entity, bool mergeAllMeshes) {
        if (!model || !entity) {
            return false;
        }

        if (model->Meshes.empty()) {
            return false;
        }

        // MaterialComponent 처리 (첫 번째 재질 사용)
        if (!model->Materials.empty() && model->Materials[0]) {
            MaterialComponent* matComp = entity->AddComponent<MaterialComponent>();
            // Material의 이름이나 다른 속성은 현재 MaterialComponent에 없으므로
            // 기본값으로 설정됩니다. 필요시 MaterialComponent를 확장할 수 있습니다.
        }

        // --- 스켈레톤 / 애니메이션 컴포넌트 생성 ---
        if (!model->Bones.empty()) {
            SkeletonComponent* skeleton = entity->AddComponent<SkeletonComponent>();
            skeleton->Bones = model->Bones;
            skeleton->BoneNameToIndex = model->BoneNameToIndex;
            skeleton->Animations = model->Animations;
            skeleton->FinalBoneTransforms.resize(model->Bones.size());
        }

        if (!model->Animations.empty()) {
            SkeletalAnimationComponent* anim = entity->AddComponent<SkeletalAnimationComponent>();
            anim->CurrentClipName = model->Animations[0].Name;  // 첫 번째 클립 자동 재생
            anim->CurrentTime = 0.0f;
            anim->PlayRate = 1.0f;
            anim->Loop = true;
            anim->Playing = true;
        }

        if (mergeAllMeshes) {
            // 모든 메시를 하나의 MeshComponent로 병합
            MeshComponent* meshComp = entity->AddComponent<MeshComponent>();
            
            uint32_t baseVertexOffset = 0;
            
            for (const auto& mesh : model->Meshes) {
                if (!mesh) continue;
                
                // Vertex 변환: ModelVertex -> MeshComponent::Vertex
                for (const auto& modelVertex : mesh->Vertices) {
                    ::Vertex meshCompVertex; // MeshComponent의 Vertex 사용
                    meshCompVertex.position = modelVertex.Pos;
                    meshCompVertex.normal = modelVertex.Normal;
                    meshCompVertex.texCoord = modelVertex.TexC;

                    // --- 추가: 본 인덱스 / 가중치 복사 ---
                    for (int i = 0; i < 4; ++i) {
                        meshCompVertex.boneIndices[i] = modelVertex.BoneIndices[i];
                        meshCompVertex.boneWeights[i] = modelVertex.BoneWeights[i];
                    }

                    meshComp->vertices.push_back(meshCompVertex);
                }
                
                // Index 변환 (baseVertexOffset 추가)
                for (uint32_t index : mesh->Indices) {
                    meshComp->indices.push_back(baseVertexOffset + index);
                }
                
                baseVertexOffset += static_cast<uint32_t>(mesh->Vertices.size());
            }
            
            meshComp->isLoaded = true;
        }
        else {
            // 각 메시마다 별도의 MeshComponent 생성
            for (const auto& mesh : model->Meshes) {
                if (!mesh) continue;
                
                MeshComponent* meshComp = entity->AddComponent<MeshComponent>();
                
                // Vertex 변환: ModelVertex -> MeshComponent::Vertex
                for (const auto& modelVertex : mesh->Vertices) {
                    ::Vertex meshCompVertex{};                   // 전체 0으로 초기화
                    meshCompVertex.position = modelVertex.Pos;
                    meshCompVertex.normal = modelVertex.Normal;
                    meshCompVertex.texCoord = modelVertex.TexC;

                    // 본/가중치 복사 (모델에서 가져온 만큼만 유효)
                    for (int i = 0; i < 4; ++i) {
                        meshCompVertex.boneIndices[i] = modelVertex.BoneIndices[i];
                        meshCompVertex.boneWeights[i] = modelVertex.BoneWeights[i];
                    }

                    // 가중치 정규화 + 디폴트 보정
                    float s = meshCompVertex.boneWeights[0] + meshCompVertex.boneWeights[1]
                        + meshCompVertex.boneWeights[2] + meshCompVertex.boneWeights[3];

                    if (s > 0.0f) {
                        float inv = 1.0f / s;
                        for (int i = 0; i < 4; ++i) meshCompVertex.boneWeights[i] *= inv;
                    }
                    else {
                        // 어떤 이유로든 전부 0이면 루트 본 하나만 영향 주도록
                        meshCompVertex.boneIndices[0] = 0;
                        meshCompVertex.boneWeights[0] = 1.0f;
                        for (int i = 1; i < 4; ++i) {
                            meshCompVertex.boneIndices[i] = 0;
                            meshCompVertex.boneWeights[i] = 0.0f;
                        }
                    }

                    meshComp->vertices.push_back(meshCompVertex);
                }
                
                // Index 복사
                meshComp->indices = mesh->Indices;
                
                meshComp->isLoaded = true;
            }
        }

        return true;
    }
}


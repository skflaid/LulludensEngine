#include "ModelInstantiation.h"
#include "Core/Entity.h"
#include "Renderer/Components/MeshComponent.h"
#include "Renderer/Components/MaterialComponent.h"
#include "Renderer/Components/SkeletonComponent.h"
#include "Renderer/Components/SkeletalAnimationComponent.h"

#include "Renderer/Model.h"

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
                
                // Index 복사
                meshComp->indices = mesh->Indices;
                
                meshComp->isLoaded = true;
            }
        }

        return true;
    }
}


#include "ModelInstantiation.h"
#include "Core/Entity.h"
#include "Renderer/Components/MeshComponent.h"
#include "Renderer/Components/MaterialComponent.h"
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

        if (mergeAllMeshes) {
            // 모든 메시를 하나의 MeshComponent로 병합
            MeshComponent* meshComp = entity->AddComponent<MeshComponent>();
            
            uint32_t baseVertexOffset = 0;
            
            for (const auto& mesh : model->Meshes) {
                if (!mesh) continue;
                
                // Vertex 변환: Model::Vertex -> MeshComponent::Vertex
                for (const auto& modelVertex : mesh->Vertices) {
                    ::Vertex meshCompVertex; // MeshComponent의 Vertex 사용
                    meshCompVertex.position = modelVertex.Pos;
                    meshCompVertex.normal = modelVertex.Normal;
                    meshCompVertex.texCoord = modelVertex.TexC;
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
                
                // Vertex 변환: Model::Vertex -> MeshComponent::Vertex
                for (const auto& modelVertex : mesh->Vertices) {
                    ::Vertex meshCompVertex; // MeshComponent의 Vertex 사용
                    meshCompVertex.position = modelVertex.Pos;
                    meshCompVertex.normal = modelVertex.Normal;
                    meshCompVertex.texCoord = modelVertex.TexC;
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


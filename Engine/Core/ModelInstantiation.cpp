#include "ModelInstantiation.h"
#include "Core/Entity.h"
#include "Renderer/Components/MeshComponent.h"
#include "Renderer/Components/MaterialComponent.h"
#include "Renderer/Components/SkeletonComponent.h"
#include "Renderer/Components/SkeletalAnimationComponent.h"

#include "Renderer/Model.h"

// ?대뵒???묎렐 ?ъ슫 cpp???뺤쟻 ?⑥닔濡??먮㈃ ?명븿 (?? ModelInstantiation.cpp ?곷떒??
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
            loc = g; // 猷⑦듃??湲濡쒕쾶=濡쒖뺄
        }
        // ?댁젣 BindTransform?먮뒗 "濡쒖뺄 諛붿씤??瑜????
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

        entity->AddComponent<MaterialComponent>();

        if (!model->Bones.empty()) {
            SkeletonComponent* skeleton = entity->AddComponent<SkeletonComponent>();
            skeleton->Bones = model->Bones;
            skeleton->BoneNameToIndex = model->BoneNameToIndex;
            skeleton->Animations = model->Animations;
            skeleton->FinalBoneTransforms.resize(model->Bones.size());
        }

        if (!model->Animations.empty()) {
            SkeletalAnimationComponent* anim = entity->AddComponent<SkeletalAnimationComponent>();
            anim->CurrentClipName = model->Animations[0].Name;
            anim->CurrentTime = 0.0f;
            anim->PlayRate = 1.0f;
            anim->Loop = true;
            anim->Playing = true;
        }

        if (mergeAllMeshes) {
            MeshComponent* meshComp = entity->AddComponent<MeshComponent>();
            uint32_t baseVertexOffset = 0;

            for (const auto& mesh : model->Meshes) {
                if (!mesh) continue;

                SubmeshTextureBinding submeshBinding;
                submeshBinding.meshName = mesh->Name.empty()
                    ? "Mesh" + std::to_string(meshComp->submeshes.size())
                    : mesh->Name;
                submeshBinding.startIndex = static_cast<uint32_t>(meshComp->indices.size());
                submeshBinding.indexCount = static_cast<uint32_t>(mesh->Indices.size());

                if (mesh->MatIndex >= 0 &&
                    mesh->MatIndex < static_cast<int>(model->Materials.size()) &&
                    model->Materials[mesh->MatIndex]) {
                    submeshBinding.albedoTextureName = model->Materials[mesh->MatIndex]->DiffuseTextureName;
                    submeshBinding.normalTextureName = model->Materials[mesh->MatIndex]->NormalTextureName;
                }

                for (const auto& modelVertex : mesh->Vertices) {
                    ::Vertex meshCompVertex{};
                    meshCompVertex.position = modelVertex.Pos;
                    meshCompVertex.normal = modelVertex.Normal;
                    meshCompVertex.texCoord = modelVertex.TexC;

                    for (int i = 0; i < 4; ++i) {
                        meshCompVertex.boneIndices[i] = modelVertex.BoneIndices[i];
                        meshCompVertex.boneWeights[i] = modelVertex.BoneWeights[i];
                    }

                    meshComp->vertices.push_back(meshCompVertex);
                }

                for (uint32_t index : mesh->Indices) {
                    meshComp->indices.push_back(baseVertexOffset + index);
                }

                meshComp->submeshes.push_back(std::move(submeshBinding));
                baseVertexOffset += static_cast<uint32_t>(mesh->Vertices.size());
            }

            if (meshComp->submeshes.empty()) {
                meshComp->submeshes.push_back({ "Mesh", 0u, static_cast<uint32_t>(meshComp->indices.size()), "", "" });
            }

            meshComp->isLoaded = true;
        }
        else {
            MeshComponent* meshComp = entity->AddComponent<MeshComponent>();

            for (const auto& mesh : model->Meshes) {
                if (!mesh) continue;

                for (const auto& modelVertex : mesh->Vertices) {
                    ::Vertex meshCompVertex{};
                    meshCompVertex.position = modelVertex.Pos;
                    meshCompVertex.normal = modelVertex.Normal;
                    meshCompVertex.texCoord = modelVertex.TexC;

                    for (int i = 0; i < 4; ++i) {
                        meshCompVertex.boneIndices[i] = modelVertex.BoneIndices[i];
                        meshCompVertex.boneWeights[i] = modelVertex.BoneWeights[i];
                    }

                    float s = meshCompVertex.boneWeights[0] + meshCompVertex.boneWeights[1]
                        + meshCompVertex.boneWeights[2] + meshCompVertex.boneWeights[3];

                    if (s > 0.0f) {
                        float inv = 1.0f / s;
                        for (int i = 0; i < 4; ++i) meshCompVertex.boneWeights[i] *= inv;
                    }
                    else {
                        meshCompVertex.boneIndices[0] = 0;
                        meshCompVertex.boneWeights[0] = 1.0f;
                        for (int i = 1; i < 4; ++i) {
                            meshCompVertex.boneIndices[i] = 0;
                            meshCompVertex.boneWeights[i] = 0.0f;
                        }
                    }

                    meshComp->vertices.push_back(meshCompVertex);
                }

                uint32_t startIndex = static_cast<uint32_t>(meshComp->indices.size());
                uint32_t baseVertexOffset = static_cast<uint32_t>(meshComp->vertices.size() - mesh->Vertices.size());
                for (uint32_t index : mesh->Indices) {
                    meshComp->indices.push_back(baseVertexOffset + index);
                }

                SubmeshTextureBinding submeshBinding;
                submeshBinding.meshName = mesh->Name.empty()
                    ? "Mesh" + std::to_string(meshComp->submeshes.size())
                    : mesh->Name;
                submeshBinding.startIndex = startIndex;
                submeshBinding.indexCount = static_cast<uint32_t>(mesh->Indices.size());

                if (mesh->MatIndex >= 0 &&
                    mesh->MatIndex < static_cast<int>(model->Materials.size()) &&
                    model->Materials[mesh->MatIndex]) {
                    submeshBinding.albedoTextureName = model->Materials[mesh->MatIndex]->DiffuseTextureName;
                    submeshBinding.normalTextureName = model->Materials[mesh->MatIndex]->NormalTextureName;
                }

                meshComp->submeshes.push_back(std::move(submeshBinding));
            }

            if (meshComp->submeshes.empty()) {
                meshComp->submeshes.push_back({ "Mesh", 0u, static_cast<uint32_t>(meshComp->indices.size()), "", "" });
            }

            meshComp->isLoaded = true;
        }

        return true;
    }
}


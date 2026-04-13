#pragma once
#include "Core/IComponent.h"
#include <d3d12.h>
#include <string>
#include <vector>
#include <DirectXMath.h>

using namespace DirectX;

struct Vertex {
    XMFLOAT3 position;
    XMFLOAT3 normal;
    XMFLOAT2 texCoord;

    // 스키닝용 정보 (지금은 최대 4개 bone 영향만 저장)
    uint32_t boneIndices[4] = { 0, 0, 0, 0 };
    float    boneWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

struct SubmeshTextureBinding {
    std::string meshName;
    uint32_t startIndex = 0;
    uint32_t indexCount = 0;
    std::string albedoTextureName;
    std::string normalTextureName;
};

struct MeshComponent : public IComponent {
    COMPONENT_TYPE(MeshComponent)

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SubmeshTextureBinding> submeshes;

    // DirectX 12 resources
    ID3D12Resource* vertexBuffer = nullptr;
    ID3D12Resource* indexBuffer = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

    bool isLoaded = false;

    void LoadFromFile(const std::string& filepath);
    bool SetTextureForMesh(const std::string& meshName, const std::string& albedoTextureName, const std::string& normalTextureName = "");
    void SetTextureForAllMeshes(const std::string& albedoTextureName, const std::string& normalTextureName = "");
    const SubmeshTextureBinding* FindSubmesh(const std::string& meshName) const;

    void CreateCube() {
        // Cube mesh with 24 vertices (4 per face) for proper normals
        vertices = {
            // Front face (z = -0.5)
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
            {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
            {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
            {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},

            // Back face (z = 0.5)
            {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
            {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
            {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},

            // Top face (y = 0.5)
            {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
            {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
            {{ 0.5f,  0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},

            // Bottom face (y = -0.5)
            {{-0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}},
            {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},
            {{ 0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},
            {{ 0.5f, -0.5f,  0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},

            // Right face (x = 0.5)
            {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
            {{ 0.5f,  0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
            {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},

            // Left face (x = -0.5)
            {{-0.5f, -0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
            {{-0.5f,  0.5f,  0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
            {{-0.5f,  0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
            {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}}
        };

        indices = {
            // Front face
            0, 1, 2, 0, 2, 3,
            // Back face
            4, 5, 6, 4, 6, 7,
            // Top face
            8, 9, 10, 8, 10, 11,
            // Bottom face
            12, 13, 14, 12, 14, 15,
            // Right face
            16, 17, 18, 16, 18, 19,
            // Left face
            20, 21, 22, 20, 22, 23
        };

        submeshes.clear();
        submeshes.push_back({ "Cube", 0u, static_cast<uint32_t>(indices.size()), "", "" });
        isLoaded = true;
    }

    void Shutdown() override {
        if (vertexBuffer) vertexBuffer->Release();
        if (indexBuffer) indexBuffer->Release();
    }
};
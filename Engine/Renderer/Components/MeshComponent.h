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

    // 스켈레탈용 (지금은 안 써도 괜찮고, 나중에 VS에서 사용할 예정)
    uint32_t boneIndices[4] = { 0, 0, 0, 0 };
    float    boneWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

struct MeshComponent : public IComponent {
    COMPONENT_TYPE(MeshComponent)

        std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // DirectX 12 resources
    ID3D12Resource* vertexBuffer = nullptr;
    ID3D12Resource* indexBuffer = nullptr;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW indexBufferView = {};

    bool isLoaded = false;

    void LoadFromFile(const std::string& filepath);

    void CreateCube() {
        // Simple cube mesh (8 vertices)
        vertices = {
            // Front face
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
            {{-0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
            {{ 0.5f,  0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
            {{ 0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},

            // Back face
            {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
            {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
            {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}}
        };

        indices = {
            // Front face
            0, 1, 2, 0, 2, 3,
            // Back face
            4, 5, 6, 4, 6, 7,
            // Left face
            0, 4, 7, 0, 7, 1,
            // Right face
            3, 2, 6, 3, 6, 5,
            // Top face
            1, 7, 6, 1, 6, 2,
            // Bottom face
            0, 3, 5, 0, 5, 4
        };

        isLoaded = true;
    }

    void Shutdown() override {
        if (vertexBuffer) vertexBuffer->Release();
        if (indexBuffer) indexBuffer->Release();
    }
};

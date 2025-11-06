#pragma once
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <wrl.h>
#include <d3d12.h>
#include <iostream>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

// 정점 데이터 구조체 (Model용 - MeshComponent::Vertex와 구분)
struct ModelVertex {
    XMFLOAT3 Pos;
    XMFLOAT3 Normal;
    XMFLOAT2 TexC; // 텍스처 좌표 추가
};

// 재질 정보 (PBR 기반을 가정하지만, 일단은 텍스처만)
struct Material {
    std::string Name;
    // 텍스처 리소스와 SRV 핸들 정보 등
    ComPtr<ID3D12Resource> DiffuseSrvHeap;
    int DiffuseSrvHeapIndex = -1;
};

// 렌더링될 메시의 최소 단위
class Mesh {
public:
    std::string Name;

    // CPU 데이터 (Entity 변환 시 사용)
    std::vector<ModelVertex> Vertices;
    std::vector<uint32_t> Indices;

    // GPU 리소스 (렌더링 시 사용)
    ComPtr<ID3D12Resource> VertexBuffer;
    ComPtr<ID3D12Resource> IndexBuffer;
    D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
    D3D12_INDEX_BUFFER_VIEW IndexBufferView;

    UINT IndexCount = 0;
    int MatIndex = -1; // 이 메시가 사용할 재질의 인덱스
};

// FBX 파일 하나에 대응되는 전체 모델 정보
class Model {
public:
    std::vector<std::unique_ptr<Mesh>> Meshes;
    std::vector<std::unique_ptr<Material>> Materials;
};
#pragma once
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <wrl.h>
#include <d3d12.h>
#include <iostream>
#include <unordered_map>
#include <memory>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

inline std::string NormalizeBoneName(const std::string& name)
{
    std::string r = name;

    // 1) '|' 뒤 자르기 (Armature|mixamorig:LeftArm)
    size_t pos = r.find_last_of('|');
    if (pos != std::string::npos && pos + 1 < r.size())
        r = r.substr(pos + 1);

    // 2) ':' 뒤 자르기 (mixamorig:LeftArm)
    pos = r.find_last_of(':');
    if (pos != std::string::npos && pos + 1 < r.size())
        r = r.substr(pos + 1);

    // 3) Assimp FBX가 붙이는 보조 노드 접미사 제거
    //    예: "LeftArm_$AssimpFbx$_Rotation" -> "LeftArm"
    const std::string assimpSuffixMarker = "_$AssimpFbx$_";
    pos = r.find(assimpSuffixMarker);
    if (pos != std::string::npos) {
        r = r.substr(0, pos);
    }

    // 4) 양쪽 공백 제거 (있을 수도 있으니)
    while (!r.empty() && (r.front() == ' ' || r.front() == '\t'))
        r.erase(r.begin());
    while (!r.empty() && (r.back() == ' ' || r.back() == '\t'))
        r.pop_back();

    // 5) 전부 소문자로
    for (auto& ch : r) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    return r;
}

// 정점 데이터 구조체 (Model용 - MeshComponent::Vertex와 구분)
struct ModelVertex {
    XMFLOAT3 Pos;
    XMFLOAT3 Normal;
    XMFLOAT2 TexC; // 텍스처 좌표 추가

    // 스켈레탈 애니메이션용 : 최대 4개 본 인덱스 + 가중치
    uint32_t BoneIndices[4] = { 0, 0, 0, 0 };
    float    BoneWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

// 재질 정보 (PBR 기반을 가정하지만, 일단은 텍스처만)
struct ModelMaterial {
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

// ===== 스켈레탈 애니메이션 데이터 =====

struct ModelBone {
    std::string Name;
    int ParentIndex = -1;             // -1이면 루트
    DirectX::XMFLOAT4X4 Offset;       // inverse bind pose (aiBone::mOffsetMatrix)
    DirectX::XMFLOAT4X4 BindTransform; //바인드 포즈
};

struct ModelKeyframeVec3 {
    double Time = 0.0;
    DirectX::XMFLOAT3 Value = { 0.0f, 0.0f, 0.0f };
};

struct ModelKeyframeQuat {
    double Time = 0.0;
    DirectX::XMFLOAT4 Value = { 0.0f, 0.0f, 0.0f, 1.0f };
};

struct ModelBoneAnimation {
    std::vector<ModelKeyframeVec3> Translations;
    std::vector<ModelKeyframeQuat> Rotations;
    std::vector<ModelKeyframeVec3> Scales;
};

struct ModelAnimationClip {
    std::string Name;
    double Duration = 0.0;   // 애니메이션 길이 (ticks)
    double TicksPerSecond = 0.0;  // 초당 tick 수
    // bone index -> animation track
    std::vector<ModelBoneAnimation> BoneAnimations;
};

// FBX 파일 하나에 대응되는 전체 모델 정보
class Model {
public:
    std::vector<std::unique_ptr<Mesh>> Meshes;
    std::vector<std::unique_ptr<ModelMaterial>> Materials;

    // --- 스켈레톤 + 애니메이션 ---
    std::vector<ModelBone> Bones;                          // 본 리스트
    std::unordered_map<std::string, int> BoneNameToIndex;  // 본 이름 -> 인덱스
    std::vector<ModelAnimationClip> Animations;            // 애니메이션 클립들

    int GetBoneIndex(const std::string& name) const {
        // 1) 원본 이름 그대로 검색
        auto it = BoneNameToIndex.find(name);
        if (it != BoneNameToIndex.end())
            return it->second;

        // 2) 정규화해서 다시 검색
        std::string norm = NormalizeBoneName(name);
        it = BoneNameToIndex.find(norm);
        if (it != BoneNameToIndex.end())
            return it->second;

        return -1;
    }

    const ModelAnimationClip* GetAnimation(const std::string& name) const {
        if (Animations.empty()) return nullptr;
        if (name.empty()) return &Animations[0];
        for (const auto& anim : Animations) {
            if (anim.Name == name) return &anim;
        }
        return &Animations[0];
    }
};
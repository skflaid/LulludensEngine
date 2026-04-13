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

    // 1) '|' ???먮Ⅴ湲?(Armature|mixamorig:LeftArm)
    size_t pos = r.find_last_of('|');
    if (pos != std::string::npos && pos + 1 < r.size())
        r = r.substr(pos + 1);

    // 2) ':' ???먮Ⅴ湲?(mixamorig:LeftArm)
    pos = r.find_last_of(':');
    if (pos != std::string::npos && pos + 1 < r.size())
        r = r.substr(pos + 1);

    // 3) Assimp FBX媛 遺숈씠??蹂댁“ ?몃뱶 ?묐????쒓굅
    //    ?? "LeftArm_$AssimpFbx$_Rotation" -> "LeftArm"
    const std::string assimpSuffixMarker = "_$AssimpFbx$_";
    pos = r.find(assimpSuffixMarker);
    if (pos != std::string::npos) {
        r = r.substr(0, pos);
    }

    // 4) ?묒そ 怨듬갚 ?쒓굅 (?덉쓣 ?섎룄 ?덉쑝??
    while (!r.empty() && (r.front() == ' ' || r.front() == '\t'))
        r.erase(r.begin());
    while (!r.empty() && (r.back() == ' ' || r.back() == '\t'))
        r.pop_back();

    // 5) ?꾨? ?뚮Ц?먮줈
    for (auto& ch : r) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    return r;
}

// ?뺤젏 ?곗씠??援ъ“泥?(Model??- MeshComponent::Vertex? 援щ텇)
struct ModelVertex {
    XMFLOAT3 Pos;
    XMFLOAT3 Normal;
    XMFLOAT2 TexC; // ?띿뒪泥?醫뚰몴 異붽?

    // ?ㅼ펷?덊깉 ?좊땲硫붿씠?섏슜 : 理쒕? 4媛?蹂??몃뜳??+ 媛以묒튂
    uint32_t BoneIndices[4] = { 0, 0, 0, 0 };
    float    BoneWeights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
};

// ?ъ쭏 ?뺣낫 (PBR 湲곕컲??媛?뺥븯吏留? ?쇰떒? ?띿뒪泥섎쭔)
struct ModelMaterial {
    std::string Name;
    std::string DiffuseTextureName;
    std::string NormalTextureName;
    // ?띿뒪泥?由ъ냼?ㅼ? SRV ?몃뱾 ?뺣낫 ??
    ComPtr<ID3D12Resource> DiffuseSrvHeap;
    int DiffuseSrvHeapIndex = -1;
};

// ?뚮뜑留곷맆 硫붿떆??理쒖냼 ?⑥쐞
class Mesh {
public:
    std::string Name;

    // CPU ?곗씠??(Entity 蹂?????ъ슜)
    std::vector<ModelVertex> Vertices;
    std::vector<uint32_t> Indices;

    // GPU 由ъ냼??(?뚮뜑留????ъ슜)
    ComPtr<ID3D12Resource> VertexBuffer;
    ComPtr<ID3D12Resource> IndexBuffer;
    D3D12_VERTEX_BUFFER_VIEW VertexBufferView;
    D3D12_INDEX_BUFFER_VIEW IndexBufferView;

    UINT IndexCount = 0;
    int MatIndex = -1; // ??硫붿떆媛 ?ъ슜???ъ쭏???몃뜳??
};

// ===== ?ㅼ펷?덊깉 ?좊땲硫붿씠???곗씠??=====

struct ModelBone {
    std::string Name;
    int ParentIndex = -1;             // -1?대㈃ 猷⑦듃
    DirectX::XMFLOAT4X4 Offset;       // inverse bind pose (aiBone::mOffsetMatrix)
    DirectX::XMFLOAT4X4 BindTransform; //諛붿씤???ъ쫰
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
    double Duration = 0.0;   // ?좊땲硫붿씠??湲몄씠 (ticks)
    double TicksPerSecond = 0.0;  // 珥덈떦 tick ??
    // bone index -> animation track
    std::vector<ModelBoneAnimation> BoneAnimations;
};

// FBX ?뚯씪 ?섎굹????묐릺???꾩껜 紐⑤뜽 ?뺣낫
class Model {
public:
    std::vector<std::unique_ptr<Mesh>> Meshes;
    std::vector<std::unique_ptr<ModelMaterial>> Materials;

    // --- ?ㅼ펷?덊넠 + ?좊땲硫붿씠??---
    std::vector<ModelBone> Bones;                          // 蹂?由ъ뒪??
    std::unordered_map<std::string, int> BoneNameToIndex;  // 蹂??대쫫 -> ?몃뜳??
    std::vector<ModelAnimationClip> Animations;            // ?좊땲硫붿씠???대┰??

    int GetBoneIndex(const std::string& name) const {
        // 1) ?먮낯 ?대쫫 洹몃?濡?寃??
        auto it = BoneNameToIndex.find(name);
        if (it != BoneNameToIndex.end())
            return it->second;

        // 2) ?뺢퇋?뷀빐???ㅼ떆 寃??
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
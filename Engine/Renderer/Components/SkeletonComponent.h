#pragma once
#include "Core/IComponent.h"
#include "Renderer/Model.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <DirectXMath.h>

using namespace DirectX;

struct SkeletonComponent : public IComponent {
    COMPONENT_TYPE(SkeletonComponent)

    // Model에서 복사한 스켈레톤/애니메이션 데이터
    std::vector<ModelBone> Bones;
    std::unordered_map<std::string, int> BoneNameToIndex;
    std::vector<ModelAnimationClip> Animations;

    // 매 프레임 계산된 최종 본 행렬 (Skinning 행렬)
    std::vector<XMFLOAT4X4> FinalBoneTransforms;

    void Initialize() override {}
    void Shutdown() override {}
};

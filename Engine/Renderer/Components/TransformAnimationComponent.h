#pragma once
#include "Core/IComponent.h"
#include <vector>
#include <DirectXMath.h>

using namespace DirectX;

// 한 키프레임: 시간 + 위치/회전/스케일
struct TransformKeyframe
{
    float   Time = 0.0f;          // 초 단위

    XMFLOAT3 Translation = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 RotationDeg = { 0.0f, 0.0f, 0.0f }; // degree 기준 (SetRotationDegrees와 맞춤)
    XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };
};

// 하나의 클립(walk, idle 등)
struct TransformAnimationClip
{
    std::vector<TransformKeyframe> Keyframes;
    float Duration = 0.0f; // 초 단위 (0이면 마지막 키프레임 타임으로 자동 세팅)
};

struct TransformAnimationComponent : public IComponent
{
    COMPONENT_TYPE(TransformAnimationComponent)

        TransformAnimationClip Clip;

    float CurrentTime = 0.0f;
    float PlayRate = 1.0f;  // 1.0 = 정속, 0.5 = 절반 속도
    bool  Loop = true;
    bool  Playing = true;

    void Initialize() override {}
    void Shutdown() override {}
};
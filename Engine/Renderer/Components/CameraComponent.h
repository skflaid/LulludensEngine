#pragma once
#include "Core/IComponent.h" // IComponent 헤더 포함
#include <DirectXMath.h>

using namespace DirectX;

struct CameraComponent : public IComponent {
    COMPONENT_TYPE(CameraComponent)

    //CameraComponent 멤버 변수들 ---
    XMFLOAT4X4 ViewMatrix;
    XMFLOAT4X4 ProjMatrix;

    float Fov = XM_PIDIV4; // 45도
    float MoveSpeed = 20.0f;
    float LookSpeed = 0.003f;
};

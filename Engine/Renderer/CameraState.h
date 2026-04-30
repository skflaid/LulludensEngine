#pragma once

#include "Core/EngineTypes.h"
#include <DirectXMath.h>

struct CameraLogicState {
    EntityId Owner = 0;
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 Rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    float FovY = DirectX::XM_PIDIV4;
    float AspectRatio = 16.0f / 9.0f;
    float NearZ = 0.1f;
    float FarZ = 1000.0f;
};

struct CameraRenderState {
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 Rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    DirectX::XMFLOAT4X4 View;
    DirectX::XMFLOAT4X4 Proj;
    DirectX::XMFLOAT4X4 ViewProj;

    CameraRenderState();
};

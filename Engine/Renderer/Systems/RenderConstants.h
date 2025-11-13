#pragma once
#include <DirectXMath.h>

using namespace DirectX;

// Constant buffer structures matching shader
// HLSL 정렬 규칙에 맞추기 위해 패킹 사용
#pragma pack(push, 16)

struct ObjectConstants {
    XMFLOAT4X4 gWorld;
    XMFLOAT4X4 gWorldInvTranspose;
};

struct RenderMaterialConstants {
    XMFLOAT4 gDiffuseAlbedo;
    XMFLOAT3 gFresnelR0;
    float gRoughness;
    XMFLOAT4X4 gMatTransform;
};

struct PassLight {
    XMFLOAT3 Strength;
    float FalloffStart;
    XMFLOAT3 Direction;
    float FalloffEnd;
    XMFLOAT3 Position;
    float SpotPower;
};

struct PassConstants {
    XMFLOAT4X4 gView;
    XMFLOAT4X4 gInvView;
    XMFLOAT4X4 gProj;
    XMFLOAT4X4 gInvProj;
    XMFLOAT4X4 gViewProj;
    XMFLOAT4X4 gInvViewProj;
    XMFLOAT3 gEyePosW;
    float cbPerObjectPad1;
    XMFLOAT2 gRenderTargetSize;
    XMFLOAT2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    XMFLOAT4 gAmbientLight;
    PassLight gLights[16];
    int gRenderMode;
    float cbPerObjectPad3;
    XMFLOAT2 cbPerObjectPad4;
};

#pragma pack(pop)

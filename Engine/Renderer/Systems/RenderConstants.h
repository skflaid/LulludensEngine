#pragma once
#include <DirectXMath.h>

using namespace DirectX;

// Constant buffer structures matching shader
// HLSL 정렬 규칙에 맞추기 위해 패킹 사용
#pragma pack(push, 16)

struct SkinningConstants {
    XMFLOAT4X4 BoneTransforms[128]; // HLSL과 동일 크기
};

struct ObjectConstants {
    XMFLOAT4X4 gWorld;
    XMFLOAT4X4 gWorldInvTranspose;
    XMFLOAT4X4 gPrevWorld;
};

struct VelocityPassConstants {
    XMFLOAT4X4 gViewProj;
    XMFLOAT4X4 gPrevViewProj;
    XMFLOAT4X4 gInvViewNoTranslation;
    XMFLOAT4X4 gInvProj;
    XMFLOAT4X4 gPrevViewNoTranslation;
    XMFLOAT4X4 gPrevProj;
    XMFLOAT2 gRenderTargetSize;
    XMFLOAT2 gInvRenderTargetSize;
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

    XMFLOAT4X4 gShadowView;
    XMFLOAT4X4 gShadowProj;
    XMFLOAT4X4 gShadowViewProj;
    XMFLOAT4X4 gShadowTransform;
    
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

struct SkyPassConstants {
    XMFLOAT4X4 gViewNoTranslation;
    XMFLOAT4X4 gProj;
    XMFLOAT4 gTint;
    float gExposure;
    float gRotationY;
    XMFLOAT2 gPadding;
};

#pragma pack(pop)

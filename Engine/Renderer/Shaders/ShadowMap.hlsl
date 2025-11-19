// ShadowMap.hlsl

#include "LightingUtil.hlsl" // Light struct, PassLight 등 정의용 (필요시)

// Object, Material, Pass 상수 버퍼 (C++과 동일 레이아웃)
cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldInvTranspose;
};

cbuffer cbMaterial : register(b1)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float  gRoughness;
    float4x4 gMatTransform;
};

cbuffer cbPass : register(b2)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;

    float4x4 gShadowView;
    float4x4 gShadowProj;
    float4x4 gShadowViewProj;
    float4x4 gShadowTransform;

    float3 gEyePosW;
    float  cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float  gNearZ;
    float  gFarZ;
    float  gTotalTime;
    float  gDeltaTime;
    float4 gAmbientLight;

    Light gLights[16];
    int   gRenderMode;
    float cbPerObjectPad3;
    float2 cbPerObjectPad4;
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);

    // 라이트 기준 shadow view/proj
    vout.PosH = mul(posW, gShadowViewProj);

    return vout;
}

// 컬러 출력 없음 → 깊이만
void PS(VertexOut pin)
{
    // 필요하면 알파 테스트 등 나중에 추가
}

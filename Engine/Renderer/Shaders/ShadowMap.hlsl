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

cbuffer cbSkinning : register(b3)
{
    float4x4 gBoneTransforms[128];
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;

    uint4  BoneIndices : BLENDINDICES; // 새로 추가
    float4 BoneWeights : BLENDWEIGHT;  // 새로 추가
};


struct VertexOut
{
    float4 PosH : SV_POSITION;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;

    float4 posL = float4(vin.PosL, 1.0f);
    float4 skinnedPosL = posL;
    float3 skinnedNormalL = vin.NormalL;

    float weightSum = vin.BoneWeights.x + vin.BoneWeights.y +
        vin.BoneWeights.z + vin.BoneWeights.w;

    if (weightSum > 0.0f)
    {
        float4x4 B0 = gBoneTransforms[vin.BoneIndices.x];
        float4x4 B1 = gBoneTransforms[vin.BoneIndices.y];
        float4x4 B2 = gBoneTransforms[vin.BoneIndices.z];
        float4x4 B3 = gBoneTransforms[vin.BoneIndices.w];

        float4x4 SkinMat =
            B0 * vin.BoneWeights.x +
            B1 * vin.BoneWeights.y +
            B2 * vin.BoneWeights.z +
            B3 * vin.BoneWeights.w;

        skinnedPosL = mul(posL, SkinMat);
        float4 nL = float4(vin.NormalL, 0.0f);
        float4 sknN = mul(nL, SkinMat);
        skinnedNormalL = sknN.xyz;
    }

    float4 posW = mul(skinnedPosL, gWorld);

    vout.PosH = mul(posW, gShadowViewProj);
    return vout;
}


// 컬러 출력 없음 → 깊이만
void PS(VertexOut pin)
{
    // 필요하면 알파 테스트 등 나중에 추가
}

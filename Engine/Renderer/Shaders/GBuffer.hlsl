// G-Buffer Pass Shader for Deferred Rendering
#include "LightingUtil.hlsl"

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
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;

    // GBuffer에선 안 쓰더라도 필드는 둠
    Light  gLights[16];   // 또는 LightingUtil.hlsl 의 Light와 동일 구조면 Light gLights[16];
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
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut)0.0f;

    float4 posL = float4(vin.PosL, 1.0f);

    // 기본: 스킨 안 된 정적 메쉬
    float4 skinnedPosL = posL;
    float3 skinnedNormalL = vin.NormalL;

    // BoneWeights 합으로 스킨 여부 판단
    float weightSum = vin.BoneWeights.x + vin.BoneWeights.y +
        vin.BoneWeights.z + vin.BoneWeights.w;

    if (weightSum > 0.0f)
    {
        // 본 행렬 4개 가져오기
        float4x4 B0 = gBoneTransforms[vin.BoneIndices.x];
        float4x4 B1 = gBoneTransforms[vin.BoneIndices.y];
        float4x4 B2 = gBoneTransforms[vin.BoneIndices.z];
        float4x4 B3 = gBoneTransforms[vin.BoneIndices.w];

        // 가중치 합으로 스키닝 행렬
        float4x4 SkinMat =
            B0 * vin.BoneWeights.x +
            B1 * vin.BoneWeights.y +
            B2 * vin.BoneWeights.z +
            B3 * vin.BoneWeights.w;

        // 위치 스키닝 (모델 공간)
        skinnedPosL = mul(posL, SkinMat);

        // 노멀 스키닝 (w=0)
        float4 nL = float4(vin.NormalL, 0.0f);
        float4 sknN = mul(nL, SkinMat);
        skinnedNormalL = sknN.xyz;
    }

    // 모델 공간 → 월드 공간
    float4 posW = mul(skinnedPosL, gWorld);
    vout.PosW = posW.xyz;

    // 노멀도 월드로
    vout.NormalW = mul(skinnedNormalL, (float3x3)gWorldInvTranspose);
    vout.NormalW = normalize(vout.NormalW);

    // 월드 → 클립
    vout.PosH = mul(posW, gViewProj);

    // UV는 그대로
    vout.TexC = vin.TexC;

    return vout;
}


// G-Buffer outputs
struct GBufferOut
{
    float4 Position : SV_Target0;  // World position
    float4 Normal   : SV_Target1;  // World normal
    float4 Albedo   : SV_Target2;  // Diffuse albedo
    float4 Material : SV_Target3;  // Roughness, Metallic, etc.
};

GBufferOut PS(VertexOut pin)
{
    GBufferOut gbuffer = (GBufferOut)0.0f;

    // Normalize interpolated normal
    pin.NormalW = normalize(pin.NormalW);

    // Position (World space)
    gbuffer.Position = float4(pin.PosW, 1.0f);

    // Normal
    gbuffer.Normal = float4(pin.NormalW * 0.5f + 0.5f, 1.0f);

    // Albedo
    gbuffer.Albedo = gDiffuseAlbedo;

    // Material properties
    gbuffer.Material = float4(gRoughness, 0.0f, gFresnelR0.x, 1.0f);

    return gbuffer;
}


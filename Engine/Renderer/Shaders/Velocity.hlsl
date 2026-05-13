cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldInvTranspose;
    float4x4 gPrevWorld;
};

cbuffer cbVelocityPass : register(b1)
{
    float4x4 gViewProj;
    float4x4 gPrevViewProj;
    float4x4 gInvViewNoTranslation;
    float4x4 gInvProj;
    float4x4 gPrevViewNoTranslation;
    float4x4 gPrevProj;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
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
    uint4  BoneIndices : BLENDINDICES;
    float4 BoneWeights : BLENDWEIGHT;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float4 CurrentClip : TEXCOORD0;
    float4 PreviousClip : TEXCOORD1;
};

struct BackgroundVertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD0;
};

float4 SkinPosition(float4 posL, uint4 boneIndices, float4 boneWeights)
{
    float weightSum = boneWeights.x + boneWeights.y + boneWeights.z + boneWeights.w;
    if (weightSum <= 0.0f) {
        return posL;
    }

    float4x4 skin =
        gBoneTransforms[boneIndices.x] * boneWeights.x +
        gBoneTransforms[boneIndices.y] * boneWeights.y +
        gBoneTransforms[boneIndices.z] * boneWeights.z +
        gBoneTransforms[boneIndices.w] * boneWeights.w;

    return mul(posL, skin);
}

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut)0.0f;

    float4 skinnedPosL = SkinPosition(float4(vin.PosL, 1.0f), vin.BoneIndices, vin.BoneWeights);
    float4 currentWorld = mul(skinnedPosL, gWorld);
    float4 previousWorld = mul(skinnedPosL, gPrevWorld);

    vout.CurrentClip = mul(currentWorld, gViewProj);
    vout.PreviousClip = mul(previousWorld, gPrevViewProj);
    vout.PosH = vout.CurrentClip;
    return vout;
}

float2 ClipToUv(float4 clipPos)
{
    float2 ndc = clipPos.xy / max(clipPos.w, 0.00001f);
    return float2(ndc.x * 0.5f + 0.5f, -ndc.y * 0.5f + 0.5f);
}

float4 PS(VertexOut pin) : SV_Target0
{
    float2 currentUv = ClipToUv(pin.CurrentClip);
    float2 previousUv = ClipToUv(pin.PreviousClip);
    float2 velocityPixels = (previousUv - currentUv) * gRenderTargetSize;
    return float4(velocityPixels, 0.0f, 1.0f);
}

BackgroundVertexOut VSBackground(uint vertexID : SV_VertexID)
{
    BackgroundVertexOut vout;
    vout.TexC = float2((vertexID << 1) & 2, vertexID & 2);
    vout.PosH = float4(vout.TexC.x * 2.0f - 1.0f, -(vout.TexC.y * 2.0f - 1.0f), 0.0f, 1.0f);
    return vout;
}

float4 PSBackground(BackgroundVertexOut pin) : SV_Target0
{
    float2 ndc = float2(pin.TexC.x * 2.0f - 1.0f, 1.0f - pin.TexC.y * 2.0f);
    float4 currentClip = float4(ndc, 1.0f, 1.0f);
    float4 currentView = mul(currentClip, gInvProj);
    float3 currentWorldDir = normalize(mul(float4(currentView.xyz, 0.0f), gInvViewNoTranslation).xyz);

    float3 previousViewDir = mul(float4(currentWorldDir, 0.0f), gPrevViewNoTranslation).xyz;
    float4 previousClip = mul(float4(previousViewDir, 1.0f), gPrevProj);

    float2 currentUv = pin.TexC;
    float2 previousUv = ClipToUv(previousClip);
    float2 velocityPixels = (previousUv - currentUv) * gRenderTargetSize;
    return float4(velocityPixels, 0.0f, 1.0f);
}

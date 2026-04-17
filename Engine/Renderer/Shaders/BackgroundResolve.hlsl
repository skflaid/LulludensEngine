#include "LightingUtil.hlsl"

cbuffer cbPass : register(b0)
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

    Light gLights[16];
    int gRenderMode;
    float cbPerObjectPad3;
    float2 cbPerObjectPad4;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD0;
};

VertexOut VS(uint vertexID : SV_VertexID)
{
    VertexOut vout;
    vout.TexC = float2((vertexID << 1) & 2, vertexID & 2);
    vout.PosH = float4(vout.TexC.x * 2.0f - 1.0f, -(vout.TexC.y * 2.0f - 1.0f), 0.0f, 1.0f);
    return vout;
}

struct ResolveOut
{
    float4 Normal : SV_Target0;
    float4 Depth  : SV_Target1;
};

ResolveOut PS(VertexOut pin)
{
    ResolveOut output;

    float2 ndc;
    ndc.x = pin.TexC.x * 2.0f - 1.0f;
    ndc.y = 1.0f - pin.TexC.y * 2.0f;

    float4 clipPos = float4(ndc.x, ndc.y, 1.0f, 1.0f);
    float4 viewPos = mul(clipPos, gInvProj);
    float3 viewDir = normalize(viewPos.xyz / max(viewPos.w, 1e-6f));
    float3 worldDir = normalize(mul(float4(viewDir, 0.0f), gInvView).xyz);
    float3 worldNormal = -worldDir;

    output.Normal = float4(worldNormal * 0.5f + 0.5f, 1.0f);
    output.Depth = float4(gFarZ, 0.0f, 0.0f, 1.0f);
    return output;
}

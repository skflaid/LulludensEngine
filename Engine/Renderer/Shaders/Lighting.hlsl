// Lighting Pass Shader for Deferred Rendering

// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 3
#endif

#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
#define NUM_SPOT_LIGHTS 0
#endif

#include "LightingUtil.hlsl"

cbuffer cbPass : register(b0)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;
    Light gLights[MaxLights];
    int gRenderMode;  // 0: Composite, 1: Lighting, 2: SSGI
    float cbPerObjectPad3;
    float2 cbPerObjectPad4;
};

// G-Buffer textures
Texture2D gPositionMap : register(t0);
Texture2D gNormalMap   : register(t1);
Texture2D gAlbedoMap   : register(t2);
Texture2D gMaterialMap : register(t3);
Texture2D gSSGIMap     : register(t4);

SamplerState gsamPointWrap : register(s0);

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

// Fullscreen quad vertices
VertexOut VS(uint vertexID : SV_VertexID)
{
    VertexOut vout;

    // Generate fullscreen triangle
    vout.TexC = float2((vertexID << 1) & 2, vertexID & 2);
    vout.PosH = float4(vout.TexC.x * 2.0f - 1.0f, -(vout.TexC.y * 2.0f - 1.0f), 0.0f, 1.0f);

    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    // Sample G-Buffer
    float4 position = gPositionMap.Sample(gsamPointWrap, pin.TexC);
    float4 normalEncoded = gNormalMap.Sample(gsamPointWrap, pin.TexC);
    float4 albedo = gAlbedoMap.Sample(gsamPointWrap, pin.TexC);
    float4 material = gMaterialMap.Sample(gsamPointWrap, pin.TexC);

    // Decode normal (from 0.5 * normal + 0.5 encoding)
    float3 normalW = normalize(normalEncoded.rgb * 2.0f - 1.0f);
    float3 posW = position.rgb;

    // Material properties
    float roughness = material.r;
    float metallic = material.g;
    float3 fresnelR0 = float3(material.b, material.b, material.b);

    // Reconstruct material
    Material mat;
    mat.DiffuseAlbedo = albedo;
    mat.FresnelR0 = fresnelR0;
    mat.Shininess = 1.0f - roughness;

    // Vector from point being lit to eye
    float3 toEyeW = normalize(gEyePosW - posW);

    // Ambient lighting
    float4 ambient = gAmbientLight * albedo;

    // Shadow factor (all 1.0 for now)
    float3 shadowFactor = float3(1.0f, 1.0f, 1.0f);

    // Compute lighting
    float4 directLight = ComputeLighting(gLights, mat, posW, normalW, toEyeW, shadowFactor);

    // SSGI 샘플링
    float4 ssgi = gSSGIMap.Sample(gsamPointWrap, pin.TexC);
    float3 ssgiContribution = ssgi.rgb * albedo.rgb;

    // 모드에 따라 다른 결과 반환
    float4 litColor;
    if (gRenderMode == 0) {
        // Composite: Lighting + SSGI
        litColor = ambient + directLight + float4(ssgiContribution, 0.0f);
    }
    else if (gRenderMode == 1) {
        // Lighting only
        litColor = ambient + directLight;
    }
    else {
        // SSGI only
        litColor = float4(ssgiContribution, 1.0f);
    }
    
    litColor.a = albedo.a;

    return litColor;
}


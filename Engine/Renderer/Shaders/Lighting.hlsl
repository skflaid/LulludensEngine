// Lighting Pass Shader for Deferred Rendering

// Defaults for number of lights.
#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 4
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

// ShadowMap SRV (RendererCore에서 t5에 바인딩)
Texture2D gShadowMap : register(t5);

SamplerState gsamPointWrap : register(s0);
SamplerComparisonState gsamShadow : register(s1); // RootSignature에서 static sampler 추가

float CalcShadowFactor(float3 posW)
{
    // world → shadow texture space
    float4 shadowPosH = mul(float4(posW, 1.0f), gShadowTransform);

    // homogeneous divide
    shadowPosH.xyz /= shadowPosH.w;

    // shadow map UV 범위 밖이면 그림자 없음(밝게)
    if (shadowPosH.x < 0.0f || shadowPosH.x > 1.0f ||
        shadowPosH.y < 0.0f || shadowPosH.y > 1.0f)
    {
        return 1.0f;
    }

    float depth = shadowPosH.z;

    // 간단 3x3 PCF
    uint width, height, levels;
    gShadowMap.GetDimensions(0, width, height, levels);
    float dx = 1.0f / (float)width;

    float2 offsets[9] = {
        float2(-dx, -dx), float2(0, -dx), float2(+dx, -dx),
        float2(-dx,  0), float2(0,  0),   float2(+dx,  0),
        float2(-dx, +dx), float2(0, +dx), float2(+dx, +dx)
    };

    float sum = 0.0f;
    [unroll]
        for (int i = 0; i < 9; ++i)
        {
            sum += gShadowMap.SampleCmpLevelZero(
                gsamShadow,
                shadowPosH.xy + offsets[i],
                depth).r;
        }

    return sum / 9.0f; // 0(완전 그림자) ~ 1(완전 밝음)
}

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

    // Shadow factor
    float shadow = CalcShadowFactor(posW);
    float4 shadowFactor = float4(shadow, shadow, shadow, shadow);

    // Compute lighting
    float4 directLight = ComputeLighting(gLights, mat, posW, normalW, toEyeW, shadowFactor);

    // SSGI 샘플링
    float4 ssgi = gSSGIMap.Sample(gsamPointWrap, pin.TexC);
    float3 ssgiContribution = ssgi.rgb;

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


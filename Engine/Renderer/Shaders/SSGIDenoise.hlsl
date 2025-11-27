// SSGI Denoise Compute Shader
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
    int   gRenderMode;
    float cbPerObjectPad3;
    float2 cbPerObjectPad4;
};

// G-Buffer textures
Texture2D gPositionMap : register(t0);
Texture2D gNormalMap   : register(t1);
Texture2D gAlbedoMap   : register(t2);
Texture2D gMaterialMap : register(t3);
// Raw SSGI input
Texture2D gSSGIInput   : register(t4);

// SSGI output
RWTexture2D<float4> gSSGIOutput : register(u0);

SamplerState gsamPointWrap : register(s0);

// Bilateral Filter 파라미터
static const float BILATERAL_SIGMA_SPATIAL = 1.0f;  // 공간적 가우시안 시그마
static const float BILATERAL_SIGMA_DEPTH = 0.1f;    // 깊이 차이 시그마
static const float BILATERAL_SIGMA_NORMAL = 0.3f;   // 노말 차이 시그마
static const int BILATERAL_KERNEL_SIZE = 5;         // 필터 커널 크기 (5x5)

// Bilateral Filter: 엣지 보존 스무딩
float3 ApplyBilateralFilter(int2 centerCoord, float3 centerPos, float3 centerNormal)
{
    float3 centerGI = gSSGIInput.Load(int3(centerCoord, 0)).rgb;
    float3 filteredGI = float3(0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;

    int halfKernel = BILATERAL_KERNEL_SIZE / 2;

    for (int y = -halfKernel; y <= halfKernel; ++y)
    {
        for (int x = -halfKernel; x <= halfKernel; ++x)
        {
            int2 sampleCoord = centerCoord + int2(x, y);
            sampleCoord = clamp(sampleCoord, int2(0, 0), int2(gRenderTargetSize) - 1);

            float4 samplePos = gPositionMap.Load(int3(sampleCoord, 0));
            float4 sampleNormalEncoded = gNormalMap.Load(int3(sampleCoord, 0));

            if (samplePos.w < 0.001f)
                continue;

            float3 sampleNormal = normalize(sampleNormalEncoded.rgb * 2.0f - 1.0f);

            float2 offset = float2(x, y);
            float spatialDist = length(offset);
            float spatialWeight = exp(-(spatialDist * spatialDist) / (2.0f * BILATERAL_SIGMA_SPATIAL * BILATERAL_SIGMA_SPATIAL));

            float depthDiff = abs(length(samplePos.xyz - centerPos));
            float depthWeight = exp(-(depthDiff * depthDiff) / (2.0f * BILATERAL_SIGMA_DEPTH * BILATERAL_SIGMA_DEPTH));

            float normalDiff = 1.0f - dot(centerNormal, sampleNormal);
            normalDiff = max(normalDiff, 0.0f);
            float normalWeight = exp(-(normalDiff * normalDiff) / (2.0f * BILATERAL_SIGMA_NORMAL * BILATERAL_SIGMA_NORMAL));

            float weight = spatialWeight * depthWeight * normalWeight;

            float3 sampleGI = gSSGIInput.Load(int3(sampleCoord, 0)).rgb;

            filteredGI += sampleGI * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight > 0.001f)
    {
        filteredGI /= totalWeight;
    }
    else
    {
        filteredGI = centerGI;
    }

    return filteredGI;
}

[numthreads(8, 8, 1)]
void CS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= (uint)gRenderTargetSize.x || dispatchThreadID.y >= (uint)gRenderTargetSize.y)
        return;

    int2 texCoord = int2(dispatchThreadID.xy);
    float4 position = gPositionMap.Load(int3(texCoord, 0));
    float4 normalEncoded = gNormalMap.Load(int3(texCoord, 0));

    float3 normalW = normalize(normalEncoded.rgb * 2.0f - 1.0f);
    float3 posW = position.rgb;

    if (position.w < 0.001f)
    {
        gSSGIOutput[dispatchThreadID.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float3 filteredSSGI = ApplyBilateralFilter(texCoord, posW, normalW);

    gSSGIOutput[dispatchThreadID.xy] = float4(filteredSSGI, 1.0f);
}

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

// G-Buffer textures (깊이, 노말 정보용)
Texture2D gPositionMap : register(t0);
Texture2D gNormalMap   : register(t1);

// SSGI 입력 (필터링 전) - SRV로 읽기
Texture2D gSSGIInput : register(t2);

// 이전 프레임 SSGI 출력 (Temporal Filter용) - SRV로 읽기
Texture2D gSSGIPrevious : register(t3);

// SSGI 출력 (필터링 후) - UAV로 쓰기
RWTexture2D<float4> gSSGIOutput : register(u0);

SamplerState gsamPointWrap : register(s0);

// Bilateral Filter 파라미터
static const float BILATERAL_SIGMA_SPATIAL = 2.0f;  // 공간적 가우시안 시그마
static const float BILATERAL_SIGMA_DEPTH = 0.2f;    // 깊이 차이 시그마
static const float BILATERAL_SIGMA_NORMAL = 0.3f;   // 노말 차이 시그마
static const int BILATERAL_KERNEL_SIZE = 7;         // 필터 커널 크기 (5x5)

// Bilateral Filter: 엣지 보존 스무딩
float3 ApplyBilateralFilter(float3 centerGI, float3 centerPos, float3 centerNormal, int2 centerCoord)
{
    float3 filteredGI = float3(0.0f, 0.0f, 0.0f);
    float totalWeight = 0.0f;
    
    int halfKernel = BILATERAL_KERNEL_SIZE / 2;
    
    // 커널 내의 모든 픽셀에 대해
    for (int y = -halfKernel; y <= halfKernel; ++y)
    {
        for (int x = -halfKernel; x <= halfKernel; ++x)
        {
            int2 sampleCoord = centerCoord + int2(x, y);
            sampleCoord = clamp(sampleCoord, int2(0, 0), int2(gRenderTargetSize) - 1);
            
            // 샘플 픽셀의 G-Buffer 정보
            float4 samplePos = gPositionMap.Load(int3(sampleCoord, 0));
            float4 sampleNormalEncoded = gNormalMap.Load(int3(sampleCoord, 0));
            
            // 배경이면 스킵
            if (samplePos.w < 0.001f)
                continue;
            
            float3 sampleNormal = normalize(sampleNormalEncoded.rgb * 2.0f - 1.0f);
            
            // 1. 공간적 거리 가중치 (Gaussian)
            float2 offset = float2(x, y);
            float spatialDist = length(offset);
            float spatialWeight = exp(-(spatialDist * spatialDist) / (2.0f * BILATERAL_SIGMA_SPATIAL * BILATERAL_SIGMA_SPATIAL));
            
            // 2. 깊이 차이 가중치
            float depthDiff = abs(length(samplePos.xyz - centerPos));
            float depthWeight = exp(-(depthDiff * depthDiff) / (2.0f * BILATERAL_SIGMA_DEPTH * BILATERAL_SIGMA_DEPTH));
            
            // 3. 노말 차이 가중치
            float normalDiff = 1.0f - dot(centerNormal, sampleNormal);
            normalDiff = max(normalDiff, 0.0f);
            float normalWeight = exp(-(normalDiff * normalDiff) / (2.0f * BILATERAL_SIGMA_NORMAL * BILATERAL_SIGMA_NORMAL));
            
            // 최종 가중치 = 공간 * 깊이 * 노말
            float weight = spatialWeight * depthWeight * normalWeight;
            
            // 샘플 픽셀의 SSGI 값 읽기 (SRV에서 읽기)
            float3 sampleGI = gSSGIInput.Load(int3(sampleCoord, 0)).rgb;
            
            filteredGI += sampleGI * weight;
            totalWeight += weight;
        }
    }
    
    // 가중 평균
    if (totalWeight > 0.001f)
    {
        filteredGI /= totalWeight;
    }
    else
    {
        filteredGI = centerGI;  // 가중치가 없으면 원본 사용
    }
    
    return filteredGI;
}

[numthreads(8, 8, 1)]
void CS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    // 화면 크기 체크
    if (dispatchThreadID.x >= (uint)gRenderTargetSize.x || dispatchThreadID.y >= (uint)gRenderTargetSize.y)
        return;
    
    // 픽셀 좌표
    int2 texCoord = int2(dispatchThreadID.xy);
    
    // G-Buffer에서 위치와 노말 정보 읽기
    float4 position = gPositionMap.Load(int3(texCoord, 0));
    float4 normalEncoded = gNormalMap.Load(int3(texCoord, 0));
    
    // 배경이면 필터링하지 않음
    if (position.w < 0.001f)
    {
        gSSGIOutput[dispatchThreadID.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    
    // 노말 디코딩
    float3 normalW = normalize(normalEncoded.rgb * 2.0f - 1.0f);
    float3 posW = position.rgb;
    
    // 현재 픽셀의 SSGI 값 읽기 (SRV에서 읽기)
    float3 centerGI = gSSGIInput.Load(int3(texCoord, 0)).rgb;
    
    // Bilateral Filter 적용 (엣지 보존 스무딩)
    float3 filteredSSGI = ApplyBilateralFilter(centerGI, posW, normalW, texCoord);
    
    // Temporal Filter: 이전 프레임 결과와 현재 결과를 lerp (0.2 : 0.8)
    float3 previousSSGI = gSSGIPrevious.Load(int3(texCoord, 0)).rgb;
    float3 temporalFilteredSSGI = lerp(previousSSGI, filteredSSGI, 0.2f);
    
    // 결과 출력 (Temporal Filter 적용된 결과를 UAV에 쓰기)
    gSSGIOutput[dispatchThreadID.xy] = float4(temporalFilteredSSGI, 1.0f);
}

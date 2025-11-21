// SSGI (Screen Space Global Illumination) Compute Shader
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

    Light gLights[16];  // 혹은 Light gLights[MaxLights]
    int   gRenderMode;
    float cbPerObjectPad3;
    float2 cbPerObjectPad4;
};

// G-Buffer textures
Texture2D gPositionMap : register(t0);
Texture2D gNormalMap   : register(t1);
Texture2D gAlbedoMap   : register(t2);
Texture2D gMaterialMap : register(t3);

// SSGI output
RWTexture2D<float4> gSSGIOutput : register(u0);

SamplerState gsamPointWrap : register(s0);

// SSGI 파라미터
static const float SSGI_RAY_STEP = 0.3f;
static const float SSGI_MAX_DISTANCE = 6.0f;
static const int SSGI_NUM_SAMPLES = 32;
static const float SSGI_INTENSITY = 0.7f;

// Bilateral Filter 파라미터
static const float BILATERAL_SIGMA_SPATIAL = 1.0f;  // 공간적 가우시안 시그마
static const float BILATERAL_SIGMA_DEPTH = 0.1f;    // 깊이 차이 시그마
static const float BILATERAL_SIGMA_NORMAL = 0.3f;   // 노말 차이 시그마
static const int BILATERAL_KERNEL_SIZE = 5;         // 필터 커널 크기 (5x5)

// 화면 공간에서 랜덤 방향 벡터 생성
float3 GetRandomDirection(float2 uv, float3 normal)
{
    // 해시 함수로 랜덤 방향 생성
    float3 random = float3(
        frac(sin(dot(uv, float2(12.9898f, 78.233f))) * 43758.5453f),
        frac(sin(dot(uv, float2(23.1407f, 2.6651f))) * 43758.5453f),
        frac(sin(dot(uv, float2(7.3891f, 1.4142f))) * 43758.5453f)
    );
    
    // 정규화하고 노말 방향으로 조정
    random = normalize(random * 2.0f - 1.0f);
    
    // 노말과 같은 방향으로 조정
    if (dot(random, normal) < 0.0f)
        random = -random;
    
    return random;
}

// 화면 공간에서 레이 마칭
float3 TraceSSGI(float3 pos, float3 normal, float2 uv, float3 albedo)
{
    float3 gi = float3(0.0f, 0.0f, 0.0f);
    
    // 여러 샘플을 사용하여 간접 조명 계산
    for (int i = 0; i < SSGI_NUM_SAMPLES; ++i)
    {
        // 랜덤 방향 생성
        float2 offset = float2(
            frac(sin(dot(uv + float(i), float2(12.9898f, 78.233f))) * 43758.5453f),
            frac(sin(dot(uv + float(i), float2(23.1407f, 2.6651f))) * 43758.5453f)
        );
        offset = offset * 2.0f - 1.0f;
        
        // 반사 방향 계산
        float3 viewDir = normalize(gEyePosW - pos);
        float3 reflectDir = reflect(-viewDir, normal);
        
        // 랜덤 방향과 반사 방향을 혼합
        float3 sampleDir = normalize(lerp(reflectDir, GetRandomDirection(uv + offset, normal), 0.5f));
        
        // 레이 마칭
        float3 rayPos = pos;
        float3 rayStep = sampleDir * SSGI_RAY_STEP;
        
        float accumulatedDistance = 0.0f;
        float3 hitColor = float3(0.0f, 0.0f, 0.0f);
        bool hit = false;
        
        for (int step = 0; step < 20; ++step)
        {
            rayPos += rayStep;
            accumulatedDistance += SSGI_RAY_STEP;
            
            if (accumulatedDistance > SSGI_MAX_DISTANCE)
                break;
            
            // 월드 공간을 화면 공간으로 변환
            float4 projPos = mul(float4(rayPos, 1.0f), gViewProj);
            projPos.xyz /= projPos.w;
            
            // NDC를 UV 좌표로 변환
            float2 sampleUV = projPos.xy * 0.5f + 0.5f;
            sampleUV.y = 1.0f - sampleUV.y;
            
            // 화면 밖이면 스킵
            if (sampleUV.x < 0.0f || sampleUV.x > 1.0f || sampleUV.y < 0.0f || sampleUV.y > 1.0f)
                break;
            
            // G-Buffer에서 샘플링 (Compute Shader에서는 Load 사용)
            int2 sampleCoord = int2(sampleUV * gRenderTargetSize);
            sampleCoord = clamp(sampleCoord, int2(0, 0), int2(gRenderTargetSize) - 1);
            float4 samplePos = gPositionMap.Load(int3(sampleCoord, 0));
            float4 sampleNormalEncoded = gNormalMap.Load(int3(sampleCoord, 0));
            float4 sampleAlbedo = gAlbedoMap.Load(int3(sampleCoord, 0));
            
            // 노말 디코딩
            float3 sampleNormal = normalize(sampleNormalEncoded.rgb * 2.0f - 1.0f);
            
            // 거리 체크
            float distanceToSample = length(samplePos.xyz - rayPos);
            
            if (distanceToSample < SSGI_RAY_STEP * 0.5f)
            {
                // 히트! 샘플의 알베도를 사용하여 간접 조명 계산
                float3 toHit = normalize(samplePos.xyz - pos);
                float ndotl = max(dot(normal, toHit), 0.0f);
                
                // 거리에 따른 감쇠
                float attenuation = 1.0f / (1.0f + accumulatedDistance * accumulatedDistance);
                
                hitColor = sampleAlbedo.rgb * ndotl * attenuation;
                hit = true;
                break;
            }
        }
        
        if (hit)
        {
            gi += hitColor;
        }
    }
    
    // 평균 계산
    gi /= float(SSGI_NUM_SAMPLES);
    
    return gi * SSGI_INTENSITY;
}

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
            
            // 샘플 픽셀의 SSGI 값 계산
            float2 sampleUV = (sampleCoord + 0.5f) * gInvRenderTargetSize;
            float4 sampleAlbedo = gAlbedoMap.Load(int3(sampleCoord, 0));
            
            // 현재 픽셀이면 이미 계산된 값 사용, 아니면 간단한 근사치 사용
            float3 sampleGI;
            if (sampleCoord.x == centerCoord.x && sampleCoord.y == centerCoord.y)
            {
                sampleGI = centerGI;  // 현재 픽셀은 이미 계산된 값 사용
            }
            else
            {
                // 간단한 근사: 알베도 기반 + 거리 기반 간접 조명 근사
                float3 toSample = normalize(samplePos.xyz - centerPos);
                float ndotl = max(dot(centerNormal, toSample), 0.0f);
                float dist = length(samplePos.xyz - centerPos);
                float attenuation = 1.0f / (1.0f + dist * dist);
                sampleGI = sampleAlbedo.rgb * ndotl * attenuation * SSGI_INTENSITY * 0.5f;
            }
            
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
    
    // 픽셀 좌표를 UV 좌표로 변환
    float2 texC = (dispatchThreadID.xy + 0.5f) * gInvRenderTargetSize;

    // G-Buffer 샘플링 (Compute Shader에서는 Load 사용)
    int2 texCoord = int2(dispatchThreadID.xy);
    float4 position = gPositionMap.Load(int3(texCoord, 0));
    float4 normalEncoded = gNormalMap.Load(int3(texCoord, 0));
    float4 albedo = gAlbedoMap.Load(int3(texCoord, 0));
    float4 material = gMaterialMap.Load(int3(texCoord, 0));
    
    // 노말 디코딩
    float3 normalW = normalize(normalEncoded.rgb * 2.0f - 1.0f);
    float3 posW = position.rgb;
    
    // 배경이면 SSGI 계산하지 않음
    if (position.w < 0.001f)
    {
        gSSGIOutput[dispatchThreadID.xy] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    
    // SSGI 계산
    float3 ssgi = TraceSSGI(posW, normalW, texC, albedo.rgb);
    
    // Bilateral Filter 적용 (엣지 보존 스무딩)
    float3 filteredSSGI = ApplyBilateralFilter(ssgi, posW, normalW, texCoord);
    
    // 결과 출력
    gSSGIOutput[dispatchThreadID.xy] = float4(filteredSSGI, 1.0f);
}


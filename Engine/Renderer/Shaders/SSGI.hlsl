// SSGI (Screen Space Global Illumination) Pass Shader

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
};

// G-Buffer textures
Texture2D gPositionMap : register(t0);
Texture2D gNormalMap   : register(t1);
Texture2D gAlbedoMap   : register(t2);
Texture2D gMaterialMap : register(t3);

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

// SSGI 파라미터
static const float SSGI_RAY_STEP = 0.1f;
static const float SSGI_MAX_DISTANCE = 2.0f;
static const int SSGI_NUM_SAMPLES = 8;
static const float SSGI_INTENSITY = 0.5f;

// 화면 공간에서 랜덤 방향 벡터 생성
float3 GetRandomDirection(float2 uv, float3 normal)
{
    // 간단한 해시 함수로 랜덤 방향 생성
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
            
            // G-Buffer에서 샘플링
            float4 samplePos = gPositionMap.Sample(gsamPointWrap, sampleUV);
            float4 sampleNormalEncoded = gNormalMap.Sample(gsamPointWrap, sampleUV);
            float4 sampleAlbedo = gAlbedoMap.Sample(gsamPointWrap, sampleUV);
            
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

float4 PS(VertexOut pin) : SV_Target
{
    // G-Buffer 샘플링
    float4 position = gPositionMap.Sample(gsamPointWrap, pin.TexC);
    float4 normalEncoded = gNormalMap.Sample(gsamPointWrap, pin.TexC);
    float4 albedo = gAlbedoMap.Sample(gsamPointWrap, pin.TexC);
    float4 material = gMaterialMap.Sample(gsamPointWrap, pin.TexC);
    
    // 노말 디코딩
    float3 normalW = normalize(normalEncoded.rgb * 2.0f - 1.0f);
    float3 posW = position.rgb;
    
    // 배경이면 SSGI 계산하지 않음
    if (position.w < 0.001f)
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    
    // SSGI 계산
    float3 ssgi = TraceSSGI(posW, normalW, pin.TexC, albedo.rgb);
    
    return float4(ssgi, 1.0f);
}


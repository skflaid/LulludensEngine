cbuffer StyleTransferConstants : register(b0)
{
    uint gSourceWidth;
    uint gSourceHeight;
    uint gTargetWidth;
    uint gTargetHeight;
};

Texture2D<float4> gLighting : register(t0);
Texture2D<float> gDepth : register(t1);
Texture2D<float4> gNormal : register(t2);

RWStructuredBuffer<float> gImageTensor : register(u0);
RWStructuredBuffer<float> gDepthTensor : register(u1);
RWStructuredBuffer<float> gNormalTensor : register(u2);
RWStructuredBuffer<uint> gDepthMinMax : register(u3);

StructuredBuffer<float> gOutputTensor : register(t3);
RWTexture2D<float4> gOutputTexture : register(u4);

uint ClampCoord(uint value, uint maxExclusive)
{
    return (maxExclusive == 0u) ? 0u : min(value, maxExclusive - 1u);
}

uint SampleSourceCoord(uint dstCoord, uint dstExtent, uint srcExtent)
{
    if (dstExtent == 0u || srcExtent == 0u) {
        return 0u;
    }

    float uv = (float(dstCoord) + 0.5f) / float(dstExtent);
    uint srcCoord = (uint)(uv * float(srcExtent));
    return ClampCoord(srcCoord, srcExtent);
}

[numthreads(1, 1, 1)]
void ResetDepthMinMaxCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x != 0u || dispatchThreadId.y != 0u || dispatchThreadId.z != 0u) {
        return;
    }

    gDepthMinMax[0] = asuint(3.402823466e+38f);
    gDepthMinMax[1] = asuint(0.0f);
}

[numthreads(8, 8, 1)]
void TensorizeCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint x = dispatchThreadId.x;
    uint y = dispatchThreadId.y;
    if (x >= gTargetWidth || y >= gTargetHeight) {
        return;
    }

    uint srcX = SampleSourceCoord(x, gTargetWidth, gSourceWidth);
    uint srcY = SampleSourceCoord(y, gTargetHeight, gSourceHeight);
    uint idx = y * gTargetWidth + x;
    uint planeSize = gTargetWidth * gTargetHeight;

    float4 lighting = gLighting.Load(int3(srcX, srcY, 0));
    gImageTensor[idx] = saturate(lighting.x);
    gImageTensor[idx + planeSize] = saturate(lighting.y);
    gImageTensor[idx + planeSize * 2u] = saturate(lighting.z);

    float depthValue = gDepth.Load(int3(srcX, srcY, 0));
    gDepthTensor[idx] = depthValue;

    if (isfinite(depthValue) && depthValue >= 0.0f) {
        uint depthBits = asuint(depthValue);
        InterlockedMin(gDepthMinMax[0], depthBits);
        InterlockedMax(gDepthMinMax[1], depthBits);
    }

    float3 normal = gNormal.Load(int3(srcX, srcY, 0)).xyz;
    gNormalTensor[idx] = normal.x;
    gNormalTensor[idx + planeSize] = normal.y;
    gNormalTensor[idx + planeSize * 2u] = normal.z;
}

[numthreads(8, 8, 1)]
void NormalizeDepthCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint x = dispatchThreadId.x;
    uint y = dispatchThreadId.y;
    if (x >= gTargetWidth || y >= gTargetHeight) {
        return;
    }

    uint idx = y * gTargetWidth + x;
    float minDepth = asfloat(gDepthMinMax[0]);
    float maxDepth = asfloat(gDepthMinMax[1]);
    float invRange = (maxDepth > minDepth) ? rcp(maxDepth - minDepth) : 1.0f;
    gDepthTensor[idx] = saturate((gDepthTensor[idx] - minDepth) * invRange);
}

[numthreads(8, 8, 1)]
void DetensorizeCS(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint x = dispatchThreadId.x;
    uint y = dispatchThreadId.y;
    if (x >= gTargetWidth || y >= gTargetHeight) {
        return;
    }

    uint srcX = SampleSourceCoord(x, gTargetWidth, gSourceWidth);
    uint srcY = SampleSourceCoord(y, gTargetHeight, gSourceHeight);
    uint idx = srcY * gSourceWidth + srcX;
    uint planeSize = gSourceWidth * gSourceHeight;

    float3 color = float3(
        saturate(gOutputTensor[idx]),
        saturate(gOutputTensor[idx + planeSize]),
        saturate(gOutputTensor[idx + planeSize * 2u]));

    gOutputTexture[uint2(x, y)] = float4(color, 1.0f);
}

Texture2D<float2> gMotionVectorMap : register(t0);
SamplerState gsamPointClamp : register(s0);

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

float3 HsvToRgb(float3 hsv)
{
    float4 k = float4(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
    float3 p = abs(frac(hsv.xxx + k.xyz) * 6.0f - k.www);
    return hsv.z * lerp(k.xxx, saturate(p - k.xxx), hsv.y);
}

float4 PS(VertexOut pin) : SV_Target
{
    float2 velocityPixels = gMotionVectorMap.Sample(gsamPointClamp, pin.TexC);
    float magnitude = length(velocityPixels);

    if (magnitude < 0.01f) {
        return float4(0.02f, 0.02f, 0.025f, 1.0f);
    }

    float angle = atan2(velocityPixels.y, velocityPixels.x);
    float hue = frac(angle / 6.2831853f + 1.0f);
    float value = saturate(magnitude / 64.0f);
    float3 color = HsvToRgb(float3(hue, 0.95f, max(value, 0.18f)));
    return float4(color, 1.0f);
}

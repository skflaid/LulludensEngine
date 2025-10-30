cbuffer SceneConstants : register(b0)
{
    float4x4 world;
    float4x4 view;
    float4x4 proj;
    float4 color;
};

struct VS_INPUT
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
};

PS_INPUT VSMain(VS_INPUT input)
{
    PS_INPUT o;
    float4 wp = mul(float4(input.pos, 1), world);
    float4 vp = mul(wp, view);
    o.pos = mul(vp, proj);
    return o;
}

float4 PSMain(PS_INPUT input) : SV_TARGET
{
    return color;
}

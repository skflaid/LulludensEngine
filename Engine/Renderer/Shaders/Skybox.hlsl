cbuffer cbSkyPass : register(b0)
{
    float4x4 gViewNoTranslation;
    float4x4 gProj;
    float4 gTint;
    float gExposure;
    float gRotationY;
    float2 gPadding;
};

TextureCube gSkyCube : register(t0);
SamplerState gsamLinearClamp : register(s0);

struct VSInput
{
    float3 PosL : POSITION;
};

struct VSOutput
{
    float4 PosH : SV_POSITION;
    float3 DirL : TEXCOORD0;
};

VSOutput VS(VSInput vin)
{
    VSOutput vout;

    float4 posV = mul(float4(vin.PosL, 1.0f), gViewNoTranslation);
    float4 posH = mul(posV, gProj);

    vout.PosH = posH.xyww;
    vout.DirL = vin.PosL;
    return vout;
}

float4 PS(VSOutput pin) : SV_Target
{
    float s = sin(gRotationY);
    float c = cos(gRotationY);

    float3 dir = normalize(pin.DirL);
    float3 rotatedDir = float3(
        dir.x * c - dir.z * s,
        dir.y,
        dir.x * s + dir.z * c);

    float4 color = gSkyCube.Sample(gsamLinearClamp, rotatedDir);
    color.rgb *= gTint.rgb * gExposure;
    return color;
}

// G-Buffer Pass Shader for Deferred Rendering

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldInvTranspose;
};

cbuffer cbMaterial : register(b1)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float  gRoughness;
    float4x4 gMatTransform;
};

cbuffer cbPass : register(b2)
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

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout = (VertexOut)0.0f;

    // Transform to world space.
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    // Transform normal to world space.
    vout.NormalW = mul(vin.NormalL, (float3x3)gWorldInvTranspose);
    vout.NormalW = normalize(vout.NormalW);

    // Transform to homogeneous clip space.
    vout.PosH = mul(posW, gViewProj);

    // Texture coordinates
    vout.TexC = vin.TexC;

    return vout;
}

// G-Buffer outputs
struct GBufferOut
{
    float4 Position : SV_Target0;  // World position
    float4 Normal   : SV_Target1;  // World normal
    float4 Albedo   : SV_Target2;  // Diffuse albedo
    float4 Material : SV_Target3;  // Roughness, Metallic, etc.
};

GBufferOut PS(VertexOut pin)
{
    GBufferOut gbuffer = (GBufferOut)0.0f;

    // Normalize interpolated normal
    pin.NormalW = normalize(pin.NormalW);

    // Position (World space)
    gbuffer.Position = float4(pin.PosW, 1.0f);

    // Normal
    gbuffer.Normal = float4(pin.NormalW * 0.5f + 0.5f, 1.0f);

    // Albedo
    gbuffer.Albedo = gDiffuseAlbedo;

    // Material properties
    gbuffer.Material = float4(gRoughness, 0.0f, gFresnelR0.x, 1.0f);

    return gbuffer;
}


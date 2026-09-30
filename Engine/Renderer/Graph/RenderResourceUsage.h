#pragma once

enum class RenderResource
{
    ShadowMap,

    GBufferPosition,
    GBufferNormal,
    GBufferAlbedo,
    GBufferMaterial,
    SceneDepth,

    SSGICurrent,
    SSGIPrevious,

    BackBuffer
};

enum class RenderResourceUsage
{
    ShaderReadPixel,
    ShaderReadCompute,

    RenderTarget,
    DepthWrite,
    DepthRead,

    UnorderedAccess,

    CopySource,
    CopyDestination,

    Present
};

struct ResourceUsage
{
    RenderResource resource;
    RenderResourceUsage usage;
};
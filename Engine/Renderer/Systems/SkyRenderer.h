#pragma once
#include "Renderer/Systems/RenderConstants.h"
#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class RendererCore;
struct CameraComponent;
struct SkyComponent;
struct TextureInfo;

class SkyRenderer
{
public:
    void Initialize(RendererCore* rendererCore);
    void Shutdown();

    void Render(
        ID3D12GraphicsCommandList* commandList,
        const CameraComponent& camera,
        const SkyComponent& sky,
        const TextureInfo& cubemap,
        UINT frameIndex);

private:
    struct SkyVertex
    {
        float x;
        float y;
        float z;
    };

    void CreateRootSignature();
    void CreatePipelineState();
    void CreateGeometry();
    void CreateConstantBuffer();

private:
    static constexpr UINT FrameCount = 2;
    static constexpr UINT TextureHeapStartIndex = 8;

    RendererCore* m_RendererCore = nullptr;
    UINT m_SkyPassConstantBufferSize = 0;

    ComPtr<ID3D12RootSignature> m_RootSignature;
    ComPtr<ID3D12PipelineState> m_PipelineState;

    ComPtr<ID3D12Resource> m_VertexBuffer;
    ComPtr<ID3D12Resource> m_IndexBuffer;
    D3D12_VERTEX_BUFFER_VIEW m_VertexBufferView = {};
    D3D12_INDEX_BUFFER_VIEW m_IndexBufferView = {};
    UINT m_IndexCount = 0;

    ComPtr<ID3D12Resource> m_SkyPassConstantBuffers[FrameCount];
    UINT8* m_SkyPassConstantBufferDataBegin[FrameCount] = {};
};

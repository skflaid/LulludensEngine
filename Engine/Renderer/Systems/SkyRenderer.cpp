#include "SkyRenderer.h"
#include "../Common/d3dUtil.h"
#include "Renderer/Components/CameraComponent.h"
#include "Renderer/Components/SkyComponent.h"
#include "Renderer/RendererCore.h"
#include "Core/TextureManager.h"
#include <DirectXMath.h>
#include <cstring>

using namespace DirectX;

namespace
{
    D3D12_HEAP_PROPERTIES MakeUploadHeapProperties()
    {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;
        return heapProps;
    }
}

void SkyRenderer::Initialize(RendererCore* rendererCore)
{
    m_RendererCore = rendererCore;

    for (UINT i = 0; i < FrameCount; ++i) {
        m_SkyPassConstantBufferDataBegin[i] = nullptr;
    }

    CreateRootSignature();
    CreatePipelineState();
    CreateGeometry();
    CreateConstantBuffer();
}

void SkyRenderer::Shutdown()
{
    for (UINT i = 0; i < FrameCount; ++i) {
        if (m_SkyPassConstantBuffers[i]) {
            m_SkyPassConstantBuffers[i]->Unmap(0, nullptr);
            m_SkyPassConstantBufferDataBegin[i] = nullptr;
        }
    }
}

void SkyRenderer::CreateRootSignature()
{
    auto* device = m_RendererCore->GetDevice();

    D3D12_DESCRIPTOR_RANGE srvTable = {};
    srvTable.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvTable.NumDescriptors = 1;
    srvTable.BaseShaderRegister = 0;
    srvTable.RegisterSpace = 0;
    srvTable.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[2] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = &srvTable;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDesc.ShaderRegister = 0;
    samplerDesc.RegisterSpace = 0;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = _countof(rootParameters);
    rootDesc.pParameters = rootParameters;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &samplerDesc;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(
        &rootDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &signature,
        &error));

    ThrowIfFailed(device->CreateRootSignature(
        0,
        signature->GetBufferPointer(),
        signature->GetBufferSize(),
        IID_PPV_ARGS(&m_RootSignature)));
}

void SkyRenderer::CreatePipelineState()
{
    auto* device = m_RendererCore->GetDevice();

    const std::wstring shaderPath = L"Renderer/Shaders/Skybox.hlsl";
    ComPtr<ID3DBlob> vs = d3dUtil::CompileShader(shaderPath, nullptr, "VS", "vs_5_0");
    ComPtr<ID3DBlob> ps = d3dUtil::CompileShader(shaderPath, nullptr, "PS", "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_RASTERIZER_DESC rasterizerDesc = {};
    rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rasterizerDesc.CullMode = D3D12_CULL_MODE_FRONT;
    rasterizerDesc.FrontCounterClockwise = FALSE;
    rasterizerDesc.DepthClipEnable = TRUE;

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    D3D12_RENDER_TARGET_BLEND_DESC rtBlendDesc = {};
    rtBlendDesc.BlendEnable = FALSE;
    rtBlendDesc.LogicOpEnable = FALSE;
    rtBlendDesc.SrcBlend = D3D12_BLEND_ONE;
    rtBlendDesc.DestBlend = D3D12_BLEND_ZERO;
    rtBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    rtBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
    rtBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
    rtBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    rtBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
    rtBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    blendDesc.RenderTarget[0] = rtBlendDesc;

    D3D12_DEPTH_STENCIL_DESC depthStencilDesc = {};
    depthStencilDesc.DepthEnable = TRUE;
    depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    depthStencilDesc.StencilEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
    psoDesc.pRootSignature = m_RootSignature.Get();
    psoDesc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    psoDesc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    psoDesc.RasterizerState = rasterizerDesc;
    psoDesc.BlendState = blendDesc;
    psoDesc.DepthStencilState = depthStencilDesc;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    psoDesc.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_PipelineState)));
}

void SkyRenderer::CreateGeometry()
{
    auto* device = m_RendererCore->GetDevice();

    const SkyVertex vertices[] = {
        { -1.0f, -1.0f, -1.0f },
        { -1.0f,  1.0f, -1.0f },
        {  1.0f,  1.0f, -1.0f },
        {  1.0f, -1.0f, -1.0f },
        { -1.0f, -1.0f,  1.0f },
        { -1.0f,  1.0f,  1.0f },
        {  1.0f,  1.0f,  1.0f },
        {  1.0f, -1.0f,  1.0f }
    };

    const uint16_t indices[] = {
        0, 1, 2, 0, 2, 3,
        7, 6, 5, 7, 5, 4,
        4, 5, 1, 4, 1, 0,
        3, 2, 6, 3, 6, 7,
        1, 5, 6, 1, 6, 2,
        4, 0, 3, 4, 3, 7
    };

    const UINT vertexBufferSize = sizeof(vertices);
    const UINT indexBufferSize = sizeof(indices);
    m_IndexCount = static_cast<UINT>(_countof(indices));

    D3D12_HEAP_PROPERTIES heapProps = MakeUploadHeapProperties();

    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = vertexBufferSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_VertexBuffer)));

    void* mappedVertexData = nullptr;
    ThrowIfFailed(m_VertexBuffer->Map(0, nullptr, &mappedVertexData));
    memcpy(mappedVertexData, vertices, vertexBufferSize);
    m_VertexBuffer->Unmap(0, nullptr);

    m_VertexBufferView.BufferLocation = m_VertexBuffer->GetGPUVirtualAddress();
    m_VertexBufferView.StrideInBytes = sizeof(SkyVertex);
    m_VertexBufferView.SizeInBytes = vertexBufferSize;

    resourceDesc.Width = indexBufferSize;
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_IndexBuffer)));

    void* mappedIndexData = nullptr;
    ThrowIfFailed(m_IndexBuffer->Map(0, nullptr, &mappedIndexData));
    memcpy(mappedIndexData, indices, indexBufferSize);
    m_IndexBuffer->Unmap(0, nullptr);

    m_IndexBufferView.BufferLocation = m_IndexBuffer->GetGPUVirtualAddress();
    m_IndexBufferView.Format = DXGI_FORMAT_R16_UINT;
    m_IndexBufferView.SizeInBytes = indexBufferSize;
}

void SkyRenderer::CreateConstantBuffer()
{
    auto* device = m_RendererCore->GetDevice();
    m_SkyPassConstantBufferSize = (sizeof(SkyPassConstants) + 255) & ~255;

    D3D12_HEAP_PROPERTIES heapProps = MakeUploadHeapProperties();
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = m_SkyPassConstantBufferSize;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    for (UINT i = 0; i < FrameCount; ++i) {
        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_SkyPassConstantBuffers[i])));

        D3D12_RANGE readRange = { 0, 0 };
        ThrowIfFailed(m_SkyPassConstantBuffers[i]->Map(
            0,
            &readRange,
            reinterpret_cast<void**>(&m_SkyPassConstantBufferDataBegin[i])));
    }
}

void SkyRenderer::Render(
    ID3D12GraphicsCommandList* commandList,
    const CameraComponent& camera,
    const SkyComponent& sky,
    const TextureInfo& cubemap,
    UINT frameIndex)
{
    if (!commandList || !cubemap.IsValid || !cubemap.IsCubeMap) {
        return;
    }

    XMFLOAT4X4 viewNoTranslation = camera.ViewMatrix;
    viewNoTranslation._41 = 0.0f;
    viewNoTranslation._42 = 0.0f;
    viewNoTranslation._43 = 0.0f;

    SkyPassConstants constants = {};
    XMStoreFloat4x4(
        &constants.gViewNoTranslation,
        XMMatrixTranspose(XMLoadFloat4x4(&viewNoTranslation)));
    XMStoreFloat4x4(
        &constants.gProj,
        XMMatrixTranspose(XMLoadFloat4x4(&camera.ProjMatrix)));
    constants.gTint = XMFLOAT4(sky.Tint.x, sky.Tint.y, sky.Tint.z, 1.0f);
    constants.gExposure = sky.Exposure;
    constants.gRotationY = sky.RotationY;

    memcpy(m_SkyPassConstantBufferDataBegin[frameIndex], &constants, sizeof(constants));

    D3D12_RESOURCE_BARRIER toRenderTarget = {};
    toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRenderTarget.Transition.pResource = m_RendererCore->GetLightingBuffer();
    toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &toRenderTarget);

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(m_RendererCore->GetWidth());
    viewport.Height = static_cast<float>(m_RendererCore->GetHeight());
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor = {};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = static_cast<LONG>(m_RendererCore->GetWidth());
    scissor.bottom = static_cast<LONG>(m_RendererCore->GetHeight());

    D3D12_CPU_DESCRIPTOR_HANDLE lightingRTV = m_RendererCore->GetLightingRTVHandle();
    D3D12_CPU_DESCRIPTOR_HANDLE depthDSV = m_RendererCore->GetDSVHeap()->GetCPUDescriptorHandleForHeapStart();

    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    commandList->OMSetRenderTargets(1, &lightingRTV, FALSE, &depthDSV);
    commandList->SetPipelineState(m_PipelineState.Get());
    commandList->SetGraphicsRootSignature(m_RootSignature.Get());

    ID3D12DescriptorHeap* heaps[] = { m_RendererCore->GetGBufferSRVHeap() };
    commandList->SetDescriptorHeaps(1, heaps);

    commandList->SetGraphicsRootConstantBufferView(
        0,
        m_SkyPassConstantBuffers[frameIndex]->GetGPUVirtualAddress());

    D3D12_GPU_DESCRIPTOR_HANDLE cubemapHandle =
        m_RendererCore->GetGBufferSRVHeap()->GetGPUDescriptorHandleForHeapStart();
    cubemapHandle.ptr +=
        static_cast<SIZE_T>(TextureHeapStartIndex + cubemap.SRVIndex) *
        m_RendererCore->GetGBufferSRVDescriptorSize();
    commandList->SetGraphicsRootDescriptorTable(1, cubemapHandle);

    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->IASetVertexBuffers(0, 1, &m_VertexBufferView);
    commandList->IASetIndexBuffer(&m_IndexBufferView);
    commandList->DrawIndexedInstanced(m_IndexCount, 1, 0, 0, 0);

    D3D12_RESOURCE_BARRIER toCopySource = {};
    toCopySource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopySource.Transition.pResource = m_RendererCore->GetLightingBuffer();
    toCopySource.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toCopySource.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toCopySource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &toCopySource);
}

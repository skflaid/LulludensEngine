// Renderer/Systems/RenderSystem.cpp
#include "RenderSystem.h"
#include "../Components/TransformComponent.h"
#include "../Components/MeshComponent.h"
#include "../Components/MaterialComponent.h"
#include "../Common/d3dUtil.h"
#include <d3dcompiler.h>

#pragma comment(lib, "d3dcompiler.lib")

RenderSystem::RenderSystem(HWND hwnd, uint32_t width, uint32_t height)
    : m_Hwnd(hwnd), m_Width(width), m_Height(height), 
      m_ObjectConstantBufferSize(0), m_MaterialConstantBufferSize(0), m_PassConstantBufferSize(0) {
    for (int i = 0; i < FrameCount; ++i) {
        m_ObjectConstantBufferDataBegin[i] = nullptr;
        m_MaterialConstantBufferDataBegin[i] = nullptr;
        m_PassConstantBufferDataBegin[i] = nullptr;
    }
}

RenderSystem::~RenderSystem() {
    Shutdown();
}

void RenderSystem::Initialize() {
    m_RendererCore = std::make_unique<RendererCore>();
    if (!m_RendererCore->Initialize(m_Hwnd, m_Width, m_Height)) {
        return;
    }

    // 카메라(뷰) 행렬 설정
    XMVECTOR eye = XMVectorSet(0.0f, 3.0f, -8.0f, 0.0f);  // 카메라 위치
    XMVECTOR at = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);    // 바라보는 지점
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);    // 상향 벡터
    XMStoreFloat4x4(&m_ViewMatrix, XMMatrixLookAtLH(eye, at, up));

    // 원근 투영 행렬 설정
    float fov = XM_PIDIV4; // 45도
    float aspectRatio = static_cast<float>(m_Width) / static_cast<float>(m_Height);
    XMStoreFloat4x4(&m_ProjMatrix, XMMatrixPerspectiveFovLH(fov, aspectRatio, 0.1f, 100.0f));

    CreateConstantBuffer();
    CreateGBufferPipelineState();
    CreateLightingPipelineState();
}

void RenderSystem::CreateConstantBuffer() {
    auto device = m_RendererCore->GetDevice();

    // 상수 버퍼는 256바이트 배수로 정렬되어야 함
    m_ObjectConstantBufferSize = (sizeof(ObjectConstants) + 255) & ~255;
    m_MaterialConstantBufferSize = (sizeof(RenderMaterialConstants) + 255) & ~255;
    m_PassConstantBufferSize = (sizeof(PassConstants) + 255) & ~255;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    // Object constant buffer (b0) - 100개 오브젝트까지 지원
    D3D12_RESOURCE_DESC resourceDesc = {};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = m_ObjectConstantBufferSize * 100;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    for (int i = 0; i < FrameCount; ++i) {
        // Object constant buffers
        device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_ObjectConstantBuffers[i])
        );
        D3D12_RANGE readRange = { 0, 0 };
        m_ObjectConstantBuffers[i]->Map(0, &readRange, reinterpret_cast<void**>(&m_ObjectConstantBufferDataBegin[i]));

        // Material constant buffers (b1) - 100개 머티리얼까지 지원
        resourceDesc.Width = m_MaterialConstantBufferSize * 100;
        device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_MaterialConstantBuffers[i])
        );
        m_MaterialConstantBuffers[i]->Map(0, &readRange, reinterpret_cast<void**>(&m_MaterialConstantBufferDataBegin[i]));

        // Pass constant buffer (b2) - 프레임당 1개
        resourceDesc.Width = m_PassConstantBufferSize;
        device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_PassConstantBuffers[i])
        );
        m_PassConstantBuffers[i]->Map(0, &readRange, reinterpret_cast<void**>(&m_PassConstantBufferDataBegin[i]));
    }
}

void RenderSystem::CreateGBufferPipelineState() {
    auto device = m_RendererCore->GetDevice();

    // Root Signature (CBV b0, b1, b2)
    D3D12_ROOT_PARAMETER rootParameters[3] = {};
    
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].Descriptor.ShaderRegister = 1;
    rootParameters[1].Descriptor.RegisterSpace = 0;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    
    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[2].Descriptor.ShaderRegister = 2;
    rootParameters[2].Descriptor.RegisterSpace = 0;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 3;
    rootDesc.pParameters = rootParameters;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig, err;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    ThrowIfFailed(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&m_GBufferRootSignature)));

    const std::wstring shaderPath = L"Renderer/Shaders/GBuffer.hlsl";
    ComPtr<ID3DBlob> vs, ps;
    vs = d3dUtil::CompileShader(shaderPath, nullptr, "VS", "vs_5_0");
    ps = d3dUtil::CompileShader(shaderPath, nullptr, "PS", "ps_5_0");

    D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = TRUE;
    for (int i = 0; i < 4; ++i) {
        blendDesc.RenderTarget[i].BlendEnable = FALSE;
        blendDesc.RenderTarget[i].LogicOpEnable = FALSE;
        blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }

    D3D12_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rastDesc.CullMode = D3D12_CULL_MODE_BACK;
    rastDesc.FrontCounterClockwise = FALSE;
    rastDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rastDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rastDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rastDesc.DepthClipEnable = TRUE;
    rastDesc.MultisampleEnable = FALSE;
    rastDesc.AntialiasedLineEnable = FALSE;
    rastDesc.ForcedSampleCount = 0;
    rastDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    dsDesc.StencilEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.InputLayout = { inputElements, _countof(inputElements) };
    pso.pRootSignature = m_GBufferRootSignature.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.RasterizerState = rastDesc;
    pso.BlendState = blendDesc;
    pso.DepthStencilState = dsDesc;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 4;
    pso.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;  // Position
    pso.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;  // Normal
    pso.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;     // Albedo
    pso.RTVFormats[3] = DXGI_FORMAT_R8G8B8A8_UNORM;     // Material
    pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pso.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_GBufferPipelineState)));
}

void RenderSystem::CreateLightingPipelineState() {
    auto device = m_RendererCore->GetDevice();

    // Root Signature (CBV b0 for Pass constants, DescriptorTable for G-Buffer SRVs)
    D3D12_DESCRIPTOR_RANGE srvTable[4] = {};
    srvTable[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvTable[0].NumDescriptors = 1;
    srvTable[0].BaseShaderRegister = 0;
    srvTable[0].RegisterSpace = 0;
    srvTable[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    srvTable[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvTable[1].NumDescriptors = 1;
    srvTable[1].BaseShaderRegister = 1;
    srvTable[1].RegisterSpace = 0;
    srvTable[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    srvTable[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvTable[2].NumDescriptors = 1;
    srvTable[2].BaseShaderRegister = 2;
    srvTable[2].RegisterSpace = 0;
    srvTable[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    srvTable[3].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvTable[3].NumDescriptors = 1;
    srvTable[3].BaseShaderRegister = 3;
    srvTable[3].RegisterSpace = 0;
    srvTable[3].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[2] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 4;
    rootParameters[1].DescriptorTable.pDescriptorRanges = srvTable;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.MipLODBias = 0;
    samplerDesc.MaxAnisotropy = 0;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDesc.ShaderRegister = 0;
    samplerDesc.RegisterSpace = 0;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = rootParameters;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &samplerDesc;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig, err;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    ThrowIfFailed(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&m_LightingRootSignature)));

    const std::wstring shaderPath = L"Renderer/Shaders/Lighting.hlsl";
    ComPtr<ID3DBlob> vs, ps;
    vs = d3dUtil::CompileShader(shaderPath, nullptr, "VS", "vs_5_0");
    ps = d3dUtil::CompileShader(shaderPath, nullptr, "PS", "ps_5_0");

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rastDesc.CullMode = D3D12_CULL_MODE_NONE;
    rastDesc.FrontCounterClockwise = FALSE;
    rastDesc.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
    rastDesc.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
    rastDesc.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
    rastDesc.DepthClipEnable = FALSE;
    rastDesc.MultisampleEnable = FALSE;
    rastDesc.AntialiasedLineEnable = FALSE;
    rastDesc.ForcedSampleCount = 0;
    rastDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = FALSE;
    dsDesc.StencilEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = m_LightingRootSignature.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.RasterizerState = rastDesc;
    pso.BlendState = blendDesc;
    pso.DepthStencilState = dsDesc;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
    pso.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_LightingPipelineState)));
}

void RenderSystem::Update(float deltaTime) {
    m_TotalTime += deltaTime;
}

void RenderSystem::Shutdown() {
    for (int i = 0; i < FrameCount; ++i) {
        if (m_ObjectConstantBuffers[i]) {
            m_ObjectConstantBuffers[i]->Unmap(0, nullptr);
            m_ObjectConstantBufferDataBegin[i] = nullptr;
        }
        if (m_MaterialConstantBuffers[i]) {
            m_MaterialConstantBuffers[i]->Unmap(0, nullptr);
            m_MaterialConstantBufferDataBegin[i] = nullptr;
        }
        if (m_PassConstantBuffers[i]) {
            m_PassConstantBuffers[i]->Unmap(0, nullptr);
            m_PassConstantBufferDataBegin[i] = nullptr;
        }
    }

    if (m_RendererCore) {
        m_RendererCore->Shutdown();
    }
}

void RenderSystem::RegisterEntity(Entity* entity) {
    if (entity->HasComponent<MeshComponent>() && entity->HasComponent<TransformComponent>()) {
        m_RenderableEntities.push_back(entity);

        // GPU에 메시 업로드
        auto* meshComp = entity->GetComponent<MeshComponent>();
        if (meshComp && meshComp->isLoaded && !meshComp->vertexBuffer) {
            auto device = m_RendererCore->GetDevice();

            // Vertex Buffer 생성
            D3D12_HEAP_PROPERTIES heapProps = {};
            heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
            D3D12_RESOURCE_DESC bufferDesc = {};
            bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bufferDesc.Width = sizeof(Vertex) * meshComp->vertices.size();
            bufferDesc.Height = 1;
            bufferDesc.DepthOrArraySize = 1;
            bufferDesc.MipLevels = 1;
            bufferDesc.SampleDesc.Count = 1;
            bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&meshComp->vertexBuffer));

            UINT8* pVertexDataBegin;
            meshComp->vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pVertexDataBegin));
            memcpy(pVertexDataBegin, meshComp->vertices.data(), sizeof(Vertex) * meshComp->vertices.size());
            meshComp->vertexBuffer->Unmap(0, nullptr);

            meshComp->vertexBufferView.BufferLocation = meshComp->vertexBuffer->GetGPUVirtualAddress();
            meshComp->vertexBufferView.StrideInBytes = sizeof(Vertex);
            meshComp->vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(Vertex) * meshComp->vertices.size());

            // Index Buffer 생성
            bufferDesc.Width = sizeof(uint32_t) * meshComp->indices.size();
            device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&meshComp->indexBuffer));

            UINT8* pIndexDataBegin;
            meshComp->indexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pIndexDataBegin));
            memcpy(pIndexDataBegin, meshComp->indices.data(), sizeof(uint32_t) * meshComp->indices.size());
            meshComp->indexBuffer->Unmap(0, nullptr);

            meshComp->indexBufferView.BufferLocation = meshComp->indexBuffer->GetGPUVirtualAddress();
            meshComp->indexBufferView.Format = DXGI_FORMAT_R32_UINT;
            meshComp->indexBufferView.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * meshComp->indices.size());
        }
    }
}

void RenderSystem::UnregisterEntity(Entity* entity) {
    m_RenderableEntities.erase(std::remove(m_RenderableEntities.begin(), m_RenderableEntities.end(), entity), m_RenderableEntities.end());
}

void RenderSystem::Render() {
    m_RendererCore->BeginFrame();

    UINT frameIndex = 0;
    UpdatePassConstants(frameIndex);

    // G-Buffer Pass
    RenderGBufferPass(frameIndex);

    // Lighting Pass
    RenderLightingPass(frameIndex);

    m_RendererCore->EndFrame();
    m_RendererCore->Present();
}

void RenderSystem::RenderGBufferPass(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();
    auto device = m_RendererCore->GetDevice();

    // Transition G-Buffer to render target state
    D3D12_RESOURCE_BARRIER barriers[4] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = m_RendererCore->GetGBufferPosition();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = m_RendererCore->GetGBufferNormal();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[2].Transition.pResource = m_RendererCore->GetGBufferAlbedo();
    barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    barriers[3].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[3].Transition.pResource = m_RendererCore->GetGBufferMaterial();
    barriers[3].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[3].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    commandList->ResourceBarrier(4, barriers);

    // Set G-Buffer render targets
    D3D12_CPU_DESCRIPTOR_HANDLE gbufferRTVs[4] = {
        m_RendererCore->GetGBufferRTVHandle(0),
        m_RendererCore->GetGBufferRTVHandle(1),
        m_RendererCore->GetGBufferRTVHandle(2),
        m_RendererCore->GetGBufferRTVHandle(3)
    };
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_RendererCore->GetDSVHeap()->GetCPUDescriptorHandleForHeapStart();

    // Clear G-Buffer
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    commandList->ClearRenderTargetView(gbufferRTVs[0], clearColor, 0, nullptr);
    commandList->ClearRenderTargetView(gbufferRTVs[1], clearColor, 0, nullptr);
    commandList->ClearRenderTargetView(gbufferRTVs[2], clearColor, 0, nullptr);
    commandList->ClearRenderTargetView(gbufferRTVs[3], clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    commandList->OMSetRenderTargets(4, gbufferRTVs, FALSE, &dsvHandle);

    // Set pipeline state
    commandList->SetPipelineState(m_GBufferPipelineState.Get());
    commandList->SetGraphicsRootSignature(m_GBufferRootSignature.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Render entities
    int objectIndex = 0;
    for (Entity* entity : m_RenderableEntities) {
        if (entity && entity->IsActive()) {
            RenderEntity(entity, frameIndex, objectIndex);
            objectIndex++;
        }
    }

    // Transition G-Buffer to pixel shader resource state
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[3].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[3].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(4, barriers);
}

void RenderSystem::RenderLightingPass(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();
    auto device = m_RendererCore->GetDevice();

    // Get back buffer resource (we need to access it through RendererCore)
    // For now, we'll get it from the RTV heap - but we need the actual resource
    // This is a workaround - ideally RendererCore should expose GetBackBuffer()
    // Get back buffer RTV
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_RendererCore->GetRTVHeap()->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += m_RendererCore->GetFrameIndex() * m_RendererCore->GetRTVDescriptorSize();

    // Note: Back buffer transition is handled in EndFrame
    // For Lighting pass, we assume back buffer is already in RENDER_TARGET state
    // If not, we need to add transition here

    // Clear back buffer
    const float clearColor[] = { 0.2f, 0.2f, 0.2f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);

    // Set render target to back buffer
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    // Set pipeline state
    commandList->SetPipelineState(m_LightingPipelineState.Get());
    commandList->SetGraphicsRootSignature(m_LightingRootSignature.Get());

    // Set pass constant buffer
    D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = m_PassConstantBuffers[frameIndex]->GetGPUVirtualAddress();
    commandList->SetGraphicsRootConstantBufferView(0, passCBAddress);

    // Set G-Buffer SRVs
    ID3D12DescriptorHeap* srvHeaps[] = { m_RendererCore->GetGBufferSRVHeap() };
    commandList->SetDescriptorHeaps(1, srvHeaps);
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = m_RendererCore->GetGBufferSRVHeap()->GetGPUDescriptorHandleForHeapStart();
    commandList->SetGraphicsRootDescriptorTable(1, srvHandle);

    // Draw fullscreen quad
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);
}

void RenderSystem::UpdatePassConstants(UINT frameIndex) {
    PassConstants passConstants = {};
    
    XMMATRIX V = XMLoadFloat4x4(&m_ViewMatrix);
    XMMATRIX P = XMLoadFloat4x4(&m_ProjMatrix);
    XMMATRIX VP = XMMatrixMultiply(V, P);
    
    XMStoreFloat4x4(&passConstants.gView, XMMatrixTranspose(V));
    XMStoreFloat4x4(&passConstants.gInvView, XMMatrixTranspose(XMMatrixInverse(nullptr, V)));
    XMStoreFloat4x4(&passConstants.gProj, XMMatrixTranspose(P));
    XMStoreFloat4x4(&passConstants.gInvProj, XMMatrixTranspose(XMMatrixInverse(nullptr, P)));
    XMStoreFloat4x4(&passConstants.gViewProj, XMMatrixTranspose(VP));
    XMStoreFloat4x4(&passConstants.gInvViewProj, XMMatrixTranspose(XMMatrixInverse(nullptr, VP)));
    
    // Eye position
    XMMATRIX invV = XMMatrixInverse(nullptr, V);
    XMStoreFloat3(&passConstants.gEyePosW, invV.r[3]);
    
    passConstants.gRenderTargetSize = XMFLOAT2(static_cast<float>(m_Width), static_cast<float>(m_Height));
    passConstants.gInvRenderTargetSize = XMFLOAT2(1.0f / m_Width, 1.0f / m_Height);
    passConstants.gNearZ = 0.1f;
    passConstants.gFarZ = 100.0f;
    passConstants.gTotalTime = m_TotalTime;
    passConstants.gDeltaTime = 0.016f;
    passConstants.gAmbientLight = m_AmbientLight;
    
    // 기본 방향광 3개 설정 (셰이더가 NUM_DIR_LIGHTS=3을 기대함)
    // 첫 번째 라이트: 위에서 아래로
    passConstants.gLights[0].Strength = XMFLOAT3(0.9f, 0.9f, 0.9f);
    passConstants.gLights[0].Direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
    passConstants.gLights[0].FalloffStart = 1.0f;
    passConstants.gLights[0].FalloffEnd = 1000.0f;
    passConstants.gLights[0].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
    passConstants.gLights[0].SpotPower = 1.0f;
    
    // 두 번째 라이트: 약간의 보조광
    passConstants.gLights[1].Strength = XMFLOAT3(0.3f, 0.3f, 0.3f);
    passConstants.gLights[1].Direction = XMFLOAT3(-0.5f, -0.5f, -0.5f);
    passConstants.gLights[1].FalloffStart = 1.0f;
    passConstants.gLights[1].FalloffEnd = 10.0f;
    passConstants.gLights[1].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
    passConstants.gLights[1].SpotPower = 64.0f;
    
    // 세 번째 라이트: 약간의 보조광
    passConstants.gLights[2].Strength = XMFLOAT3(0.2f, 0.2f, 0.2f);
    passConstants.gLights[2].Direction = XMFLOAT3(0.5f, -0.5f, 0.5f);
    passConstants.gLights[2].FalloffStart = 1.0f;
    passConstants.gLights[2].FalloffEnd = 10.0f;
    passConstants.gLights[2].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
    passConstants.gLights[2].SpotPower = 64.0f;
    
    // 나머지 라이트는 0으로 초기화
    for (int i = 3; i < 16; ++i) {
        passConstants.gLights[i].Strength = XMFLOAT3(0.0f, 0.0f, 0.0f);
        passConstants.gLights[i].Direction = XMFLOAT3(0.0f, 0.0f, 0.0f);
        passConstants.gLights[i].FalloffStart = 0.0f;
        passConstants.gLights[i].FalloffEnd = 0.0f;
        passConstants.gLights[i].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
        passConstants.gLights[i].SpotPower = 0.0f;
    }
    
    memcpy(m_PassConstantBufferDataBegin[frameIndex], &passConstants, sizeof(PassConstants));
}

void RenderSystem::RenderEntity(Entity* entity, UINT frameIndex, int objectIndex) {
    auto transform = entity->GetComponent<TransformComponent>();
    auto mesh = entity->GetComponent<MeshComponent>();
    auto material = entity->GetComponent<MaterialComponent>();

    if (!transform || !mesh || !material || !mesh->vertexBuffer) {
        return;
    }

    auto commandList = m_RendererCore->GetCommandList();

    // Object constants (b0)
    ObjectConstants objConstants = {};
    XMMATRIX W = transform->GetWorldMatrix();
    XMMATRIX WIT = XMMatrixTranspose(XMMatrixInverse(nullptr, W));
    XMStoreFloat4x4(&objConstants.gWorld, XMMatrixTranspose(W));
    XMStoreFloat4x4(&objConstants.gWorldInvTranspose, WIT);
    
    memcpy(m_ObjectConstantBufferDataBegin[frameIndex] + (objectIndex * m_ObjectConstantBufferSize), 
           &objConstants, sizeof(ObjectConstants));
    
    D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = m_ObjectConstantBuffers[frameIndex]->GetGPUVirtualAddress() 
                                           + (objectIndex * m_ObjectConstantBufferSize);
    commandList->SetGraphicsRootConstantBufferView(0, objCBAddress);

    // Material constants (b1)
    RenderMaterialConstants matConstants = {};
    matConstants.gDiffuseAlbedo = material->albedo;
    
    // FresnelR0 계산: metallic 값에 따라 보간
    XMFLOAT3 dielectricF0 = XMFLOAT3(0.04f, 0.04f, 0.04f);
    XMVECTOR f0Dielectric = XMLoadFloat3(&dielectricF0);
    XMVECTOR f0Metal = XMLoadFloat4(&material->albedo);
    XMVECTOR f0 = XMVectorLerp(f0Dielectric, f0Metal, material->metallic);
    XMStoreFloat3(&matConstants.gFresnelR0, f0);
    
    matConstants.gRoughness = material->roughness;
    XMStoreFloat4x4(&matConstants.gMatTransform, XMMatrixIdentity());
    
    memcpy(m_MaterialConstantBufferDataBegin[frameIndex] + (objectIndex * m_MaterialConstantBufferSize), 
           &matConstants, sizeof(RenderMaterialConstants));
    
    D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = m_MaterialConstantBuffers[frameIndex]->GetGPUVirtualAddress() 
                                            + (objectIndex * m_MaterialConstantBufferSize);
    commandList->SetGraphicsRootConstantBufferView(1, matCBAddress);

    // Pass constants (b2) - 프레임당 한 번만 바인딩
    if (objectIndex == 0) {
        D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = m_PassConstantBuffers[frameIndex]->GetGPUVirtualAddress();
        commandList->SetGraphicsRootConstantBufferView(2, passCBAddress);
    }

    // 메시 렌더링
    commandList->IASetVertexBuffers(0, 1, &mesh->vertexBufferView);
    commandList->IASetIndexBuffer(&mesh->indexBufferView);
    commandList->DrawIndexedInstanced(static_cast<UINT>(mesh->indices.size()), 1, 0, 0, 0);
}

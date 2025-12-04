#include "App/GameEngine.h"
#include "RenderSystem.h"
#include "../Components/TransformComponent.h"
#include "../Components/MeshComponent.h"
#include "../Components/MaterialComponent.h"
#include "../Common/d3dUtil.h"
#include "Renderer/Components/CameraComponent.h"
#include <d3dcompiler.h>
#include "Renderer/Components/SkeletonComponent.h"
#include "Core/TextureManager.h"

#pragma comment(lib, "d3dcompiler.lib")

RenderSystem::RenderSystem(GameEngine* engine, HWND hwnd, uint32_t width, uint32_t height)
    : m_Engine(engine), m_Hwnd(hwnd), m_Width(width), m_Height(height),
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

    /*
    // 카메라(뷰) 행렬 설정
    XMVECTOR eye = XMVectorSet(0.0f, 3.0f, -8.0f, 0.0f);  // 카메라 위치
    XMVECTOR at = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);    // 바라보는 지점
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);    // 상향 벡터
    XMStoreFloat4x4(&m_ViewMatrix, XMMatrixLookAtLH(eye, at, up));

    // 원근 투영 행렬 설정
    float fov = XM_PIDIV4; // 45도
    float aspectRatio = static_cast<float>(m_Width) / static_cast<float>(m_Height);
    XMStoreFloat4x4(&m_ProjMatrix, XMMatrixPerspectiveFovLH(fov, aspectRatio, 0.1f, 100.0f));
    */

    CreateConstantBuffer();
    CreateShadowResources();
    
    // TextureManager 초기화
    auto textureManager = TextureManager::Get();
    textureManager->SetSRVHeap(m_RendererCore->GetGBufferSRVHeap(), m_RendererCore->GetGBufferSRVDescriptorSize());
    
    // 기본 텍스처 로드 (white1x1.dds)
    auto device = m_RendererCore->GetDevice();
    auto commandList = m_RendererCore->GetCommandList();
    
    // Command list 열기
    commandList->Reset(m_RendererCore->GetCommandAllocator(0), nullptr);
    
    if (!textureManager->LoadDefaultTexture(device, commandList)) {
        // 기본 텍스처 로드 실패 - 에러 출력 (디버그 빌드에서만)
        #ifdef _DEBUG
        OutputDebugStringA("Warning: Failed to load default texture (white1x1.dds)\n");
        #endif
    }
    
    // Command list 실행 및 대기
    commandList->Close();
    ID3D12CommandQueue* commandQueue = m_RendererCore->GetCommandQueue();
    ID3D12CommandList* cmdLists[] = { commandList };
    commandQueue->ExecuteCommandLists(1, cmdLists);
    
    // GPU 동기화 (텍스처 업로드 완료 대기)
    // 텍스처 업로드가 완료될 때까지 대기하여 CommandAllocator 리셋 문제 방지
    m_RendererCore->FlushCommandQueue();
    
    CreateGBufferPipelineState();
    CreateShadowPipelineState();
    CreateLightingPipelineState();
    CreateSSGIPipelineState();
    CreateSSGIDenoisePipelineState();
}

void RenderSystem::CreateConstantBuffer() {
    auto device = m_RendererCore->GetDevice();

    // 상수 버퍼는 256바이트 배수로 정렬되어야 함
    m_ObjectConstantBufferSize = (sizeof(ObjectConstants) + 255) & ~255;
    m_MaterialConstantBufferSize = (sizeof(RenderMaterialConstants) + 255) & ~255;
    m_PassConstantBufferSize = (sizeof(PassConstants) + 255) & ~255;
    m_SkinningConstantBufferSize = (sizeof(SkinningConstants) + 255) & ~255;

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

        resourceDesc.Width = m_SkinningConstantBufferSize;
        device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_SkinningConstantBuffers[i])
        );
        m_SkinningConstantBuffers[i]->Map(0, &readRange,
            reinterpret_cast<void**>(&m_SkinningConstantBufferDataBegin[i]));
    }
}

void RenderSystem::CreateGBufferPipelineState() {
    auto device = m_RendererCore->GetDevice();

    // 텍스처 SRV 테이블 (알비도, 노말맵)
    D3D12_DESCRIPTOR_RANGE srvTable[2] = {};
    srvTable[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvTable[0].NumDescriptors = 1;
    srvTable[0].BaseShaderRegister = 0; // t0
    srvTable[0].RegisterSpace = 0;
    srvTable[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    
    srvTable[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvTable[1].NumDescriptors = 1;
    srvTable[1].BaseShaderRegister = 1; // t1
    srvTable[1].RegisterSpace = 0;
    srvTable[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // Root Signature (CBV b0, b1, b2, b3, SRV Table)
    D3D12_ROOT_PARAMETER rootParameters[5] = {};
    
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

    // b3 : cbSkinning (본 팔레트)
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].Descriptor.ShaderRegister = 3; // b3
    rootParameters[3].Descriptor.RegisterSpace = 0;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    
    // 텍스처 SRV 테이블
    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[4].DescriptorTable.NumDescriptorRanges = 2;
    rootParameters[4].DescriptorTable.pDescriptorRanges = srvTable;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // 샘플러 설정
    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.MipLODBias = 0;
    samplerDesc.MaxAnisotropy = 0;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDesc.ShaderRegister = 0; // s0
    samplerDesc.RegisterSpace = 0;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = _countof(rootParameters);
    rootDesc.pParameters = rootParameters;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &samplerDesc;
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
        { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT,  0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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

    // Root Signature (CBV b0 for Pass constants, DescriptorTable for G-Buffer SRVs + SSGI SRV)
    D3D12_DESCRIPTOR_RANGE srvTable[6] = {};
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

    srvTable[4].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvTable[4].NumDescriptors = 1;
    srvTable[4].BaseShaderRegister = 4;
    srvTable[4].RegisterSpace = 0;
    srvTable[4].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    srvTable[5].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvTable[5].NumDescriptors = 1;
    srvTable[5].BaseShaderRegister = 5;
    srvTable[5].RegisterSpace = 0;
    srvTable[5].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[2] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 6;
    rootParameters[1].DescriptorTable.pDescriptorRanges = srvTable;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    // s0 : 기존 GBuffer/SSGI용 포인트 샘플러
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[0].ShaderRegister = 0;
    samplers[0].RegisterSpace = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1 : ShadowMap용 비교 샘플러
    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[1].MinLOD = 0.0f;
    samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
    samplers[1].ShaderRegister = 1;
    samplers[1].RegisterSpace = 0;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 2;
    rootDesc.pParameters = rootParameters;
    rootDesc.NumStaticSamplers = 2;              // ★ 2개
    rootDesc.pStaticSamplers = samplers;
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

void RenderSystem::CreateSSGIPipelineState() {
    auto device = m_RendererCore->GetDevice();

    // Root Signature (CBV b0 for Pass constants, DescriptorTable for G-Buffer SRVs, UAV for output)
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

    D3D12_DESCRIPTOR_RANGE uavTable[1] = {};
    uavTable[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavTable[0].NumDescriptors = 1;
    uavTable[0].BaseShaderRegister = 0;
    uavTable[0].RegisterSpace = 0;
    uavTable[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[3] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 4;
    rootParameters[1].DescriptorTable.pDescriptorRanges = srvTable;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[2].DescriptorTable.pDescriptorRanges = uavTable;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

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
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 3;
    rootDesc.pParameters = rootParameters;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &samplerDesc;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> sig, err;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    ThrowIfFailed(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&m_SSGIRootSignature)));

    const std::wstring shaderPath = L"Renderer/Shaders/SSGI.hlsl";
    ComPtr<ID3DBlob> cs;
    cs = d3dUtil::CompileShader(shaderPath, nullptr, "CS", "cs_5_0");

    D3D12_COMPUTE_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = m_SSGIRootSignature.Get();
    pso.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };

    ThrowIfFailed(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&m_SSGIPipelineState)));
}

void RenderSystem::CreateSSGIDenoisePipelineState() {
    auto device = m_RendererCore->GetDevice();

    // Root Signature (CBV b0 for Pass constants, DescriptorTable for Position, Normal, SSGI Input SRV, SSGI Output UAV)
    D3D12_DESCRIPTOR_RANGE srvTable0[1] = {};
    srvTable0[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvTable0[0].NumDescriptors = 1;
    srvTable0[0].BaseShaderRegister = 0;  // Position (t0)
    srvTable0[0].RegisterSpace = 0;
    srvTable0[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE srvTable1[1] = {};
    srvTable1[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvTable1[0].NumDescriptors = 1;
    srvTable1[0].BaseShaderRegister = 1;  // Normal (t1)
    srvTable1[0].RegisterSpace = 0;
    srvTable1[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE srvTable2[1] = {};
    srvTable2[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvTable2[0].NumDescriptors = 1;
    srvTable2[0].BaseShaderRegister = 2;  // SSGI Input (t2)
    srvTable2[0].RegisterSpace = 0;
    srvTable2[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE srvTable3[1] = {};
    srvTable3[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvTable3[0].NumDescriptors = 1;
    srvTable3[0].BaseShaderRegister = 3;  // SSGI Previous (t3)
    srvTable3[0].RegisterSpace = 0;
    srvTable3[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uavTable[1] = {};
    uavTable[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavTable[0].NumDescriptors = 1;
    uavTable[0].BaseShaderRegister = 0;  // SSGI Output (u0)
    uavTable[0].RegisterSpace = 0;
    uavTable[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameters[6] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[1].DescriptorTable.pDescriptorRanges = srvTable0;  // Position
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[2].DescriptorTable.pDescriptorRanges = srvTable1;  // Normal
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[3].DescriptorTable.pDescriptorRanges = srvTable2;  // SSGI Input SRV
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[4].DescriptorTable.pDescriptorRanges = srvTable3;  // SSGI Previous SRV
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[5].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[5].DescriptorTable.pDescriptorRanges = uavTable;  // SSGI Output UAV
    rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

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
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 6;  // CBV(0) + Position SRV(1) + Normal SRV(2) + SSGI Input SRV(3) + SSGI Previous SRV(4) + SSGI Output UAV(5)
    rootDesc.pParameters = rootParameters;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &samplerDesc;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> sig, err;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    ThrowIfFailed(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&m_SSGIDenoiseRootSignature)));

    const std::wstring shaderPath = L"Renderer/Shaders/SSGIDenoise.hlsl";
    ComPtr<ID3DBlob> cs;
    cs = d3dUtil::CompileShader(shaderPath, nullptr, "CS", "cs_5_0");

    D3D12_COMPUTE_PIPELINE_STATE_DESC pso = {};
    pso.pRootSignature = m_SSGIDenoiseRootSignature.Get();
    pso.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };

    ThrowIfFailed(device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&m_SSGIDenoisePipelineState)));
}

void RenderSystem::CreateShadowResources() {
    auto device = m_RendererCore->GetDevice();

    // Shadow depth 텍스처 리소스 생성 (R24G8 typeless)
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = m_ShadowMapSize;
    texDesc.Height = m_ShadowMapSize;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE optClear = {};
    optClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    optClear.DepthStencil.Depth = 1.0f;
    optClear.DepthStencil.Stencil = 0;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,    // 나중에 DEPTH_WRITE ↔ GENERIC_READ 전환
        &optClear,
        IID_PPV_ARGS(&m_ShadowMap)));

    // DSV heap 1개짜리
    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&m_ShadowDsvHeap)));

    m_ShadowDsv = m_ShadowDsvHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvView = {};
    dsvView.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvView.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvView.Flags = D3D12_DSV_FLAG_NONE;
    dsvView.Texture2D.MipSlice = 0;

    device->CreateDepthStencilView(m_ShadowMap.Get(), &dsvView, m_ShadowDsv);

    // Shadow viewport / scissor
    m_ShadowViewport.TopLeftX = 0.0f;
    m_ShadowViewport.TopLeftY = 0.0f;
    m_ShadowViewport.Width = static_cast<float>(m_ShadowMapSize);
    m_ShadowViewport.Height = static_cast<float>(m_ShadowMapSize);
    m_ShadowViewport.MinDepth = 0.0f;
    m_ShadowViewport.MaxDepth = 1.0f;

    m_ShadowScissorRect.left = 0;
    m_ShadowScissorRect.top = 0;
    m_ShadowScissorRect.right = static_cast<LONG>(m_ShadowMapSize);
    m_ShadowScissorRect.bottom = static_cast<LONG>(m_ShadowMapSize);

    // ★ SRV는 G-Buffer SRV Heap 안에서 RendererCore가 만들어줘야 함
    // DXGI_FORMAT_R24_UNORM_X8_TYPELESS 포맷으로 SRV 생성해서 Lighting.hlsl t5에 바인딩.

    // ShadowMap SRV: DSV는 D24_UNORM_S8_UINT, SRV는 R24_UNORM_X8_TYPELESS 로 만들어야 함.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    // GBuffer SRV heap의 t5 자리에 생성 (Position=t0, Normal=t1, Albedo=t2, Material=t3, SSGI=t4, Shadow=t5)
    auto shadowSrvHandle = m_RendererCore->GetGBufferSRVHandle(5);
    device->CreateShaderResourceView(m_ShadowMap.Get(), &srvDesc, shadowSrvHandle);
}

void RenderSystem::CreateShadowPipelineState() {
    auto device = m_RendererCore->GetDevice();

    const std::wstring shaderPath = L"Renderer/Shaders/ShadowMap.hlsl";
    ComPtr<ID3DBlob> vs, ps;
    vs = d3dUtil::CompileShader(shaderPath, nullptr, "VS", "vs_5_0");
    ps = d3dUtil::CompileShader(shaderPath, nullptr, "PS", "ps_5_0");

    // GBuffer와 동일한 인풋 레이아웃
    D3D12_INPUT_ELEMENT_DESC inputElements[] = {
        { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT,  0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;

    D3D12_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rastDesc.CullMode = D3D12_CULL_MODE_BACK;
    rastDesc.FrontCounterClockwise = FALSE;
    rastDesc.DepthBias = 100000;              // ★ depth bias
    rastDesc.DepthBiasClamp = 0.0f;
    rastDesc.SlopeScaledDepthBias = 1.0f;
    rastDesc.DepthClipEnable = TRUE;
    rastDesc.MultisampleEnable = FALSE;
    rastDesc.AntialiasedLineEnable = FALSE;
    rastDesc.ForcedSampleCount = 0;
    rastDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    dsDesc.StencilEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.InputLayout = { inputElements, _countof(inputElements) };
    pso.pRootSignature = m_GBufferRootSignature.Get();         // ★ GBuffer rootSig 재사용
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.RasterizerState = rastDesc;
    pso.BlendState = blendDesc;
    pso.DepthStencilState = dsDesc;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    pso.NumRenderTargets = 0;                                // ★ 컬러 RT 없음
    pso.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;           // CreateShadowResources와 맞춰야 함
    pso.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_ShadowPipelineState)));
}


void RenderSystem::Update(float deltaTime) {
    m_TotalTime += deltaTime;

    // 1) 스켈레톤 가진 엔티티 하나 찾기
    Entity* skinnedEntity = nullptr;
    for (Entity* e : m_RenderableEntities) {
        if (e && e->HasComponent<SkeletonComponent>()) {
            skinnedEntity = e;
            break;
        }
    }
    if (!skinnedEntity) return;

    auto* skeleton = skinnedEntity->GetComponent<SkeletonComponent>();
    if (!skeleton) return;
    if (skeleton->FinalBoneTransforms.empty()) return;

    // 2) FinalBoneTransforms → SkinningConstants 복사
    SkinningConstants skin = {};
    const uint32_t boneCount = (std::min)(
        static_cast<uint32_t>(skeleton->FinalBoneTransforms.size()),
        static_cast<uint32_t>(MAX_BONES)
        );

    for (size_t i = 0; i < boneCount; ++i) {
        XMMATRIX M = XMLoadFloat4x4(&skeleton->FinalBoneTransforms[i]);
        XMStoreFloat4x4(&skin.BoneTransforms[i], XMMatrixTranspose(M));
    }
    // 남는 슬롯은 Identity로
    for (size_t i = boneCount; i < MAX_BONES; ++i) {
        DirectX::XMStoreFloat4x4(
            &skin.BoneTransforms[i],
            DirectX::XMMatrixIdentity()
        );
    }

    // 3) 모든 프레임의 cbSkinning 버퍼에 써주기 (FrameCount 개)
    for (UINT frame = 0; frame < FrameCount; ++frame) {
        memcpy(
            m_SkinningConstantBufferDataBegin[frame],
            &skin,
            sizeof(SkinningConstants)
        );
    }
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

    RenderShadowPass(frameIndex);

    // G-Buffer Pass
    RenderGBufferPass(frameIndex);

    // SSGI Pass (항상 실행)
    RenderSSGIPass(frameIndex);

    // SSGI Denoise Pass (SSGI 결과를 필터링)
    RenderSSGIDenoisePass(frameIndex);

    // 현재 SSGI 결과를 이전 프레임 텍스처로 복사 (Temporal Filter용)
    CopySSGIToPrevious(frameIndex);

    // Lighting Pass (모드에 따라 다른 결과 표시)
    RenderLightingPass(frameIndex);

    m_RendererCore->EndFrame();
    m_RendererCore->Present();
}

void RenderSystem::RenderShadowPass(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();

    // Shadow viewport & scissor
    commandList->RSSetViewports(1, &m_ShadowViewport);
    commandList->RSSetScissorRects(1, &m_ShadowScissorRect);

    // Shadow map을 DEPTH_WRITE 상태로
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_ShadowMap.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_GENERIC_READ;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // 깊이만 클리어
    commandList->ClearDepthStencilView(m_ShadowDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    commandList->OMSetRenderTargets(0, nullptr, FALSE, &m_ShadowDsv);

    commandList->SetPipelineState(m_ShadowPipelineState.Get());
    commandList->SetGraphicsRootSignature(m_GBufferRootSignature.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    int objectIndex = 0;
    for (Entity* entity : m_RenderableEntities) {
        if (entity && entity->IsActive()) {
            // Object/Material/Pass CBV 셋업 + draw
            RenderEntity(entity, frameIndex, objectIndex);
            ++objectIndex;
        }
    }

    // 다시 샘플링용 상태로
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    commandList->ResourceBarrier(1, &barrier);
}


void RenderSystem::RenderGBufferPass(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();
    auto device = m_RendererCore->GetDevice();

    // ShadowPass에서 바뀐 뷰포트/시저를 메인 화면 기준으로 복원
    D3D12_VIEWPORT mainViewport = {};
    mainViewport.TopLeftX = 0.0f;
    mainViewport.TopLeftY = 0.0f;
    mainViewport.Width = static_cast<float>(m_Width);
    mainViewport.Height = static_cast<float>(m_Height);
    mainViewport.MinDepth = 0.0f;
    mainViewport.MaxDepth = 1.0f;

    D3D12_RECT mainScissor = {};
    mainScissor.left = 0;
    mainScissor.top = 0;
    mainScissor.right = static_cast<LONG>(m_Width);
    mainScissor.bottom = static_cast<LONG>(m_Height);

    commandList->RSSetViewports(1, &mainViewport);
    commandList->RSSetScissorRects(1, &mainScissor);

    // Transition G-Buffer to render target state
    // barrier 배열을 항상 초기화 (나중에 다시 사용하기 위해)
    D3D12_RESOURCE_BARRIER barriers[4] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = m_RendererCore->GetGBufferPosition();
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = m_RendererCore->GetGBufferNormal();
    barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[2].Transition.pResource = m_RendererCore->GetGBufferAlbedo();
    barriers[3].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[3].Transition.pResource = m_RendererCore->GetGBufferMaterial();
    
    // 첫 프레임에서는 G-Buffer가 이미 RENDER_TARGET 상태로 시작
    if (!m_IsFirstGBufferFrame) {
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[3].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[3].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commandList->ResourceBarrier(4, barriers);
    }
    m_IsFirstGBufferFrame = false;

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
    // barrier 배열 재초기화 (안전하게)
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = m_RendererCore->GetGBufferPosition();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = m_RendererCore->GetGBufferNormal();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    
    barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[2].Transition.pResource = m_RendererCore->GetGBufferAlbedo();
    barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    
    barriers[3].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[3].Transition.pResource = m_RendererCore->GetGBufferMaterial();
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

void RenderSystem::ToggleRenderMode() {
    switch (m_RenderMode) {
    case RenderMode::Composite:
        m_RenderMode = RenderMode::Lighting;
        break;
    case RenderMode::Lighting:
        m_RenderMode = RenderMode::SSGI;
        break;
    case RenderMode::SSGI:
        m_RenderMode = RenderMode::Composite;
        break;
    }
}

void RenderSystem::RenderSSGIPass(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();
    auto device = m_RendererCore->GetDevice();

    // Transition SSGI buffer to unordered access state
    // 첫 프레임에서는 이미 UNORDERED_ACCESS 상태이므로 barrier를 건너뜀
    D3D12_RESOURCE_BARRIER barrier = {};
    if (!m_IsFirstSSGIFrame) {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_RendererCore->GetSSGIBuffer();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);
    }

    // Set pipeline state
    commandList->SetPipelineState(m_SSGIPipelineState.Get());
    commandList->SetComputeRootSignature(m_SSGIRootSignature.Get());

    // Set pass constant buffer
    D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = m_PassConstantBuffers[frameIndex]->GetGPUVirtualAddress();
    commandList->SetComputeRootConstantBufferView(0, passCBAddress);

    // Set descriptor heap (G-Buffer SRV Heap에 모든 descriptor가 포함되어 있음)
    ID3D12DescriptorHeap* heaps[] = { m_RendererCore->GetGBufferSRVHeap() };
    commandList->SetDescriptorHeaps(1, heaps);

    // Set G-Buffer SRVs
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = m_RendererCore->GetGBufferSRVHeap()->GetGPUDescriptorHandleForHeapStart();
    commandList->SetComputeRootDescriptorTable(1, srvHandle);

    // Set SSGI UAV (G-Buffer SRV Heap에서 가져옴)
    D3D12_GPU_DESCRIPTOR_HANDLE uavHandle = m_RendererCore->GetSSGIUAVHandleFromGBufferHeap();
    commandList->SetComputeRootDescriptorTable(2, uavHandle);

    // Dispatch compute shader (8x8 thread groups)
    uint32_t width = m_RendererCore->GetWidth();
    uint32_t height = m_RendererCore->GetHeight();
    uint32_t dispatchX = (width + 7) / 8;
    uint32_t dispatchY = (height + 7) / 8;
    commandList->Dispatch(dispatchX, dispatchY, 1);

    // SSGI 버퍼는 UNORDERED_ACCESS 상태로 유지
    // Denoise 패스에서 SRV로 읽기 위해 상태 전환할 예정
    
    // 첫 프레임 플래그 해제
    m_IsFirstSSGIFrame = false;
}

void RenderSystem::RenderSSGIDenoisePass(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();
    auto device = m_RendererCore->GetDevice();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_RendererCore->GetSSGIBuffer();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;  // SSGI 패스에서 UAV로 출력한 상태
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;  // Denoise에서 SRV로 읽기
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // Set pipeline state
    commandList->SetPipelineState(m_SSGIDenoisePipelineState.Get());
    commandList->SetComputeRootSignature(m_SSGIDenoiseRootSignature.Get());

    // Set pass constant buffer
    D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = m_PassConstantBuffers[frameIndex]->GetGPUVirtualAddress();
    commandList->SetComputeRootConstantBufferView(0, passCBAddress);

    // Set descriptor heap (G-Buffer SRV Heap에 모든 descriptor가 포함되어 있음)
    ID3D12DescriptorHeap* heaps[] = { m_RendererCore->GetGBufferSRVHeap() };
    commandList->SetDescriptorHeaps(1, heaps);

    // Set G-Buffer SRVs (Position=t0, Normal=t1)
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = m_RendererCore->GetGBufferSRVHeap()->GetGPUDescriptorHandleForHeapStart();
    
    // Position (index 0)
    commandList->SetComputeRootDescriptorTable(1, srvHandle);
    
    // Normal (index 1)
    D3D12_GPU_DESCRIPTOR_HANDLE normalHandle = srvHandle;
    normalHandle.ptr += m_RendererCore->GetGBufferSRVDescriptorSize();
    commandList->SetComputeRootDescriptorTable(2, normalHandle);
    
    // Set SSGI Input SRV (G-Buffer SRV Heap의 4번째 슬롯)
    D3D12_GPU_DESCRIPTOR_HANDLE ssgiSrvHandle = m_RendererCore->GetSSGISRVHandleFromGBufferHeap();
    commandList->SetComputeRootDescriptorTable(3, ssgiSrvHandle);
    
    // Set SSGI Previous SRV (G-Buffer SRV Heap의 7번째 슬롯)
    D3D12_GPU_DESCRIPTOR_HANDLE ssgiPreviousSrvHandle = m_RendererCore->GetSSGIPreviousSRVHandleFromGBufferHeap();
    commandList->SetComputeRootDescriptorTable(4, ssgiPreviousSrvHandle);
    
    // Set SSGI Output UAV (G-Buffer SRV Heap의 6번째 슬롯)
    D3D12_GPU_DESCRIPTOR_HANDLE uavHandle = m_RendererCore->GetSSGIUAVHandleFromGBufferHeap();
    commandList->SetComputeRootDescriptorTable(5, uavHandle);

    // SSGI 버퍼를 UAV로 쓰기 위해 상태 전환
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;  // Denoise 출력용
    commandList->ResourceBarrier(1, &barrier);

    // Dispatch compute shader (8x8 thread groups)
    uint32_t width = m_RendererCore->GetWidth();
    uint32_t height = m_RendererCore->GetHeight();
    uint32_t dispatchX = (width + 7) / 8;
    uint32_t dispatchY = (height + 7) / 8;
    commandList->Dispatch(dispatchX, dispatchY, 1);

    // SSGI 버퍼를 pixel shader resource 상태로 전환 (Lighting 패스에서 사용)
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
}

void RenderSystem::CopySSGIToPrevious(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();
    
    // 현재 SSGI 버퍼를 COPY_SOURCE 상태로 전환
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = m_RendererCore->GetSSGIBuffer();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    
    // 이전 프레임 SSGI 버퍼를 COPY_DEST 상태로 전환
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = m_RendererCore->GetSSGIPreviousBuffer();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    
    commandList->ResourceBarrier(2, barriers);
    
    // 텍스처 복사
    commandList->CopyResource(
        m_RendererCore->GetSSGIPreviousBuffer(),
        m_RendererCore->GetSSGIBuffer()
    );
    
    // 상태를 원래대로 복원
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    
    commandList->ResourceBarrier(2, barriers);
}

void RenderSystem::UpdatePassConstants(UINT frameIndex) {
    PassConstants passConstants = {};
    
    // GameEngine에서 메인 카메라를 가져옵니다.
    Entity* mainCamera = m_Engine->GetMainCamera();
    if (!mainCamera) {
        // 카메라가 없으면 렌더링 중단 (또는 기본 행렬 사용)
        return;
    }    
    
    auto cameraComp = mainCamera->GetComponent<CameraComponent>();
    if (!cameraComp) return;

    // RenderSystem의 멤버 변수 대신, CameraComponent의 행렬을 직접 가져옵니다.
    XMMATRIX V = XMLoadFloat4x4(&cameraComp->ViewMatrix);
    XMMATRIX P = XMLoadFloat4x4(&cameraComp->ProjMatrix);
    XMMATRIX VP = XMMatrixMultiply(V, P);
    
    XMStoreFloat4x4(&passConstants.gView, XMMatrixTranspose(V));
    XMStoreFloat4x4(&passConstants.gInvView, XMMatrixTranspose(XMMatrixInverse(nullptr, V)));
    XMStoreFloat4x4(&passConstants.gProj, XMMatrixTranspose(P));
    XMStoreFloat4x4(&passConstants.gInvProj, XMMatrixTranspose(XMMatrixInverse(nullptr, P)));
    XMStoreFloat4x4(&passConstants.gViewProj, XMMatrixTranspose(VP));
    XMStoreFloat4x4(&passConstants.gInvViewProj, XMMatrixTranspose(XMMatrixInverse(nullptr, VP)));
    
    // === 여기부터 라이트 기준 Shadow 행렬 계산 ===
    // 1) 방향광 0번의 방향 사용
    XMVECTOR lightDir = XMVector3Normalize(
        XMLoadFloat3(&passConstants.gLights[0].Direction)
    );

    // 혹시 0벡터면 기본 방향 사용
    if (XMVector3Less(XMVector3LengthSq(lightDir), XMVectorReplicate(0.001f)))
    {
        lightDir = XMVectorSet(0.577f, -0.577f, 0.577f, 0.0f);
    }

    // 2) 섀도우가 비출 타겟 위치 (일단 월드 원점 근처로)
    XMVECTOR targetPos = XMVectorZero();

    // 라이트 위치 = 타겟 - dir * distance
    const float lightDist = 50.0f; // 씬 규모 보고 적당히 조절
    XMVECTOR lightPos = XMVectorMultiplyAdd(
        XMVectorReplicate(-lightDist),
        lightDir,
        targetPos
    );

    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, up);

    // 3) 직교 프로젝션 (섀도우 범위)
    float l = -50.0f, r = 50.0f;
    float b = -50.0f, t = 50.0f;
    float n = 1.0f, f = 150.0f;
    XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

    XMMATRIX lightViewProj = lightView * lightProj;

    XMStoreFloat4x4(&passConstants.gShadowView, XMMatrixTranspose(lightView));
    XMStoreFloat4x4(&passConstants.gShadowProj, XMMatrixTranspose(lightProj));
    XMStoreFloat4x4(&passConstants.gShadowViewProj, XMMatrixTranspose(lightViewProj));

    // 4) NDC(-1~1) → 텍스처(0~1) 변환 행렬
    XMMATRIX T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f
    );

    XMMATRIX shadowTransform = lightViewProj * T;
    XMStoreFloat4x4(&passConstants.gShadowTransform,
        XMMatrixTranspose(shadowTransform));

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
    passConstants.gRenderMode = static_cast<int>(m_RenderMode);
    passConstants.cbPerObjectPad3 = 0.0f;
    passConstants.cbPerObjectPad4 = XMFLOAT2(0.0f, 0.0f);
    
    // 기본 방향광 4개 설정 (셰이더가 NUM_DIR_LIGHTS=4을 기대함)
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
    
    // 네 번째 라이트: Z방향 뒤에서 오는 라이트
    passConstants.gLights[3].Strength = XMFLOAT3(0.4f, 0.4f, 0.4f);
    passConstants.gLights[3].Direction = XMFLOAT3(0.0f, 0.0f, -1.0f);
    passConstants.gLights[3].FalloffStart = 1.0f;
    passConstants.gLights[3].FalloffEnd = 1000.0f;
    passConstants.gLights[3].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
    passConstants.gLights[3].SpotPower = 1.0f;
    
    // 나머지 라이트는 0으로 초기화
    for (int i = 4; i < 16; ++i) {
        passConstants.gLights[i].Strength = XMFLOAT3(0.0f, 0.0f, 0.0f);
        passConstants.gLights[i].Direction = XMFLOAT3(0.0f, 0.0f, 0.0f);
        passConstants.gLights[i].FalloffStart = 0.0f;
        passConstants.gLights[i].FalloffEnd = 0.0f;
        passConstants.gLights[i].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
        passConstants.gLights[i].SpotPower = 0.0f;
    }



    // 마지막에 memcpy 그대로 유지
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

    // Skinning constants (b3) - 일단 전 오브젝트 공통으로 frameIndex 기준 0번만 사용
    D3D12_GPU_VIRTUAL_ADDRESS skinCBAddress =
        m_SkinningConstantBuffers[frameIndex]->GetGPUVirtualAddress();
    commandList->SetGraphicsRootConstantBufferView(3, skinCBAddress);

    // 텍스처 바인딩 (root parameter 4)
    auto textureManager = TextureManager::Get();
    
    // 알비도 텍스처 가져오기 (없으면 기본 텍스처 사용)
    TextureInfo* albedoTexture = textureManager->GetTexture(material->albedoTextureName);
    if (!albedoTexture || !albedoTexture->IsValid) {
        albedoTexture = textureManager->GetTexture("white1x1");
    }
    
    // 노말맵 텍스처 가져오기 (없으면 기본 텍스처 사용)
    TextureInfo* normalTexture = textureManager->GetTexture(material->normalTextureName);
    if (!normalTexture || !normalTexture->IsValid) {
        normalTexture = textureManager->GetTexture("white1x1");
    }
    
    // 기본 텍스처가 없으면 에러 (셰이더에서 텍스처를 샘플링하므로 필수)
    if (!albedoTexture || !normalTexture || !albedoTexture->IsValid || !normalTexture->IsValid) {
        #ifdef _DEBUG
        OutputDebugStringA("Error: Default texture (white1x1) not loaded! Texture binding failed.\n");
        #endif
        // 텍스처 없이 렌더링하면 크래시가 발생할 수 있으므로 리턴
        return;
    }
    
    // SRV 힙 설정 (이미 설정되어 있을 수 있지만 안전하게)
    ID3D12DescriptorHeap* srvHeaps[] = { m_RendererCore->GetGBufferSRVHeap() };
    commandList->SetDescriptorHeaps(1, srvHeaps);
    
    // 텍스처 SRV 바인딩
    // GBuffer SRV 힙의 시작은 인덱스 8부터 (0-7은 G-Buffer, SSGI, Shadow용)
    D3D12_GPU_DESCRIPTOR_HANDLE baseHandle = m_RendererCore->GetGBufferSRVHeap()->GetGPUDescriptorHandleForHeapStart();
    UINT descriptorSize = m_RendererCore->GetGBufferSRVDescriptorSize();
    
    // Root signature에서 t0, t1 두 개의 SRV를 연속된 범위로 정의했으므로
    // 알비도 텍스처의 핸들을 바인딩하면 t0=알비도, t1=알비도+1이 됨
    // 노말맵이 알비도 다음 슬롯에 있어야 하므로, 두 텍스처가 같은 경우(기본 텍스처)는 문제없음
    // 다른 텍스처를 사용하는 경우, 노말맵이 알비도 다음 슬롯에 있어야 함
    D3D12_GPU_DESCRIPTOR_HANDLE albedoHandle = baseHandle;
    albedoHandle.ptr += (8 + albedoTexture->SRVIndex) * descriptorSize;
    
    // 알비도와 노말맵이 같은 텍스처인 경우 (기본 텍스처 사용)
    if (albedoTexture->SRVIndex == normalTexture->SRVIndex) {
        // 같은 텍스처를 t0, t1에 바인딩 (연속된 슬롯이므로 문제없음)
        commandList->SetGraphicsRootDescriptorTable(4, albedoHandle);
    } else {
        // 다른 텍스처를 사용하는 경우, 노말맵이 알비도 다음 슬롯에 있어야 함
        // 현재는 알비도 텍스처를 바인딩하고 노말맵이 다음 슬롯에 있다고 가정
        // TODO: 텍스처 로딩 시 연속된 슬롯에 배치하도록 수정 필요
        commandList->SetGraphicsRootDescriptorTable(4, albedoHandle);
    }

    // 메시 렌더링
    commandList->IASetVertexBuffers(0, 1, &mesh->vertexBufferView);
    commandList->IASetIndexBuffer(&mesh->indexBufferView);
    commandList->DrawIndexedInstanced(static_cast<UINT>(mesh->indices.size()), 1, 0, 0, 0);
}

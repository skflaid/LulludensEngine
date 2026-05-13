#include "App/GameEngine.h"
#include "Inference/WinMLStyleTransferSystem.h"
#include "RenderSystem.h"
#include "../Components/TransformComponent.h"
#include "../Components/MeshComponent.h"
#include "../Components/MaterialComponent.h"
#include "../Common/d3dUtil.h"
#include "Renderer/Components/CameraComponent.h"
#include "Renderer/Components/SkyComponent.h"
#include "DirectSRUpscaler.h"
#include <d3dcompiler.h>
#include "Renderer/Components/SkeletonComponent.h"
#include "Core/TextureManager.h"
#include "EnvironmentManager.h"
#include "SkyRenderer.h"
#include "Threading/SnapshotBuffer.h"
#include <algorithm>
#include <stdexcept>

#pragma comment(lib, "d3dcompiler.lib")

namespace {
void DebugLogDirectSR(const char* message)
{
#if defined(_DEBUG)
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
#else
    (void)message;
#endif
}

void DebugLogDirectSR(const std::string& message)
{
    DebugLogDirectSR(message.c_str());
}

std::string DirectSRTextureDesc(ID3D12Resource* texture)
{
    if (!texture) {
        return "null";
    }

    const D3D12_RESOURCE_DESC desc = texture->GetDesc();
    return std::to_string(static_cast<UINT>(desc.Width)) + "x" + std::to_string(desc.Height);
}
}

RenderSystem::RenderSystem(GameEngine* engine, HWND hwnd, uint32_t width, uint32_t height)
    : m_Engine(engine), m_Hwnd(hwnd), m_Width(width), m_Height(height),
      m_ObjectConstantBufferSize(0), m_MaterialConstantBufferSize(0), m_PassConstantBufferSize(0) {
    for (int i = 0; i < FrameCount; ++i) {
        m_ObjectConstantBufferDataBegin[i] = nullptr;
        m_MaterialConstantBufferDataBegin[i] = nullptr;
        m_PassConstantBufferDataBegin[i] = nullptr;
        m_VelocityPassConstantBufferDataBegin[i] = nullptr;
    }

    XMStoreFloat4x4(&m_CurrentViewProjMatrix, XMMatrixIdentity());
    XMStoreFloat4x4(&m_PreviousViewProjMatrix, XMMatrixIdentity());
    XMStoreFloat4x4(&m_CurrentViewMatrix, XMMatrixIdentity());
    XMStoreFloat4x4(&m_PreviousViewMatrix, XMMatrixIdentity());
    XMStoreFloat4x4(&m_CurrentProjMatrix, XMMatrixIdentity());
    XMStoreFloat4x4(&m_PreviousProjMatrix, XMMatrixIdentity());
}

RenderSystem::~RenderSystem() {
    Shutdown();
}

void RenderSystem::Initialize() {
    m_RendererCore = std::make_unique<RendererCore>();
    if (!m_RendererCore->Initialize(m_Hwnd, m_Width, m_Height)) {
        throw std::runtime_error("RendererCore initialization failed.");
    }

    /*
    // 기본 카메라 view 행렬 설정.
    XMVECTOR eye = XMVectorSet(0.0f, 3.0f, -8.0f, 0.0f);  // 카메라 위치
    XMVECTOR at = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);    // 바라볼 지점
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);    // 위쪽 벡터
    XMStoreFloat4x4(&m_ViewMatrix, XMMatrixLookAtLH(eye, at, up));

    // 기본 perspective projection 행렬 설정.
    float fov = XM_PIDIV4; // 45도
    float aspectRatio = static_cast<float>(m_Width) / static_cast<float>(m_Height);
    XMStoreFloat4x4(&m_ProjMatrix, XMMatrixPerspectiveFovLH(fov, aspectRatio, 0.1f, 100.0f));
    */

    CreateConstantBuffer();
    CreateShadowResources();
    
    InitializeTextures();

    m_EnvironmentManager = std::make_unique<EnvironmentManager>();
    m_EnvironmentManager->Initialize(TextureManager::Get());

    m_SkyRenderer = std::make_unique<SkyRenderer>();
    m_SkyRenderer->Initialize(m_RendererCore.get());

    WinMLStyleTransferSystem::Config styleConfig = {};
    styleConfig.modelPath = L"C:\\LocalRepository\\CapstoneDesign\\Learning\\net4\\net4.onnx";
    styleConfig.inputWidth = 640;
    styleConfig.inputHeight = 360;
    m_StyleOutputWidth = styleConfig.inputWidth != 0 ? styleConfig.inputWidth : m_Width;
    m_StyleOutputHeight = styleConfig.inputHeight != 0 ? styleConfig.inputHeight : m_Height;
    m_WinMLStyleTransferSystem = std::make_unique<WinMLStyleTransferSystem>(m_RendererCore.get(), styleConfig);
    m_WinMLStyleTransferSystem->Initialize();

    CreateDirectSRResources();
    DirectSRUpscaler::InitializeDesc directSRDesc = {};
    directSRDesc.Device = m_RendererCore->GetDevice();
    directSRDesc.CommandQueue = m_RendererCore->GetCommandQueue();
    directSRDesc.TargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    directSRDesc.SourceColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    directSRDesc.SourceDepthFormat = DXGI_FORMAT_R32_FLOAT;
    directSRDesc.CreateFlags = static_cast<DSR_SUPERRES_CREATE_ENGINE_FLAGS>(
        DSR_SUPERRES_CREATE_ENGINE_FLAG_ENABLE_SHARPENING |
        DSR_SUPERRES_CREATE_ENGINE_FLAG_MOTION_VECTORS_USE_TARGET_DIMENSIONS);
    m_DirectSRUpscaler = std::make_unique<DirectSRUpscaler>();
    if (!m_DirectSRUpscaler->Initialize(directSRDesc)) {
#if defined(_DEBUG)
        OutputDebugStringA(("DirectSR disabled: " + m_DirectSRUpscaler->GetLastError() + "\n").c_str());
#endif
        m_DirectSRUpscaler.reset();
    }
    
    CreateGBufferPipelineState();
    CreateVelocityPipelineState();
    CreateMotionVectorDebugPipelineState();
    CreateShadowPipelineState();
    CreateLightingPipelineState();
    CreateBackgroundResolvePipelineState();
    CreateSSGIPipelineState();
    CreateSSGIDenoisePipelineState();
}

void RenderSystem::InitializeTextures() {
    // TextureManager가 G-Buffer SRV heap 뒤쪽에 material texture SRV를 할당하도록 연결한다.
    auto textureManager = TextureManager::Get();
    textureManager->SetSRVHeap(m_RendererCore->GetGBufferSRVHeap(), m_RendererCore->GetGBufferSRVDescriptorSize());
    
    auto device = m_RendererCore->GetDevice();
    auto commandList = m_RendererCore->GetCommandList();
    
    // 텍스처 업로드용 command list를 연다.
    commandList->Reset(m_RendererCore->GetCommandAllocator(0), nullptr);
    
    if (!textureManager->LoadAllDDSFromDirectory(device, commandList)) {
        #ifdef _DEBUG
        OutputDebugStringA("Warning: Failed to load DDS textures from directory\n");
        #endif
    }
    
    // 업로드 command list를 실행하고 완료를 기다린다.
    commandList->Close();
    ID3D12CommandQueue* commandQueue = m_RendererCore->GetCommandQueue();
    ID3D12CommandList* cmdLists[] = { commandList };
    commandQueue->ExecuteCommandLists(1, cmdLists);
    
    m_RendererCore->FlushCommandQueue();
}

void RenderSystem::CreateConstantBuffer() {
    auto device = m_RendererCore->GetDevice();

    // D3D12 constant buffer는 256바이트 배수로 정렬해야 한다.
    m_ObjectConstantBufferSize = (sizeof(ObjectConstants) + 255) & ~255;
    m_MaterialConstantBufferSize = (sizeof(RenderMaterialConstants) + 255) & ~255;
    m_PassConstantBufferSize = (sizeof(PassConstants) + 255) & ~255;
    m_VelocityPassConstantBufferSize = (sizeof(VelocityPassConstants) + 255) & ~255;
    m_SkinningConstantBufferSize = (sizeof(SkinningConstants) + 255) & ~255;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

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
        // Object constant buffer (b0): 프레임당 최대 100개 object.
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

        resourceDesc.Width = m_VelocityPassConstantBufferSize;
        device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_VelocityPassConstantBuffers[i])
        );
        m_VelocityPassConstantBuffers[i]->Map(0, &readRange,
            reinterpret_cast<void**>(&m_VelocityPassConstantBufferDataBegin[i]));

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

    // 알베도/노멀 텍스처는 submesh마다 달라질 수 있어 별도 descriptor table로 둔다.
    D3D12_DESCRIPTOR_RANGE srvTableAlbedo[1] = {};
    srvTableAlbedo[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvTableAlbedo[0].NumDescriptors = 1;
    srvTableAlbedo[0].BaseShaderRegister = 0; // t0
    srvTableAlbedo[0].RegisterSpace = 0;
    srvTableAlbedo[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    
    D3D12_DESCRIPTOR_RANGE srvTableNormal[1] = {};
    srvTableNormal[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvTableNormal[0].NumDescriptors = 1;
    srvTableNormal[0].BaseShaderRegister = 1; // t1
    srvTableNormal[0].RegisterSpace = 0;
    srvTableNormal[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    // Root Signature: object/material/pass/skinning CBV와 albedo/normal SRV table.
    D3D12_ROOT_PARAMETER rootParameters[6] = {};
    
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

    // b3: skinned mesh용 bone transform 상수 버퍼.
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].Descriptor.ShaderRegister = 3; // b3
    rootParameters[3].Descriptor.RegisterSpace = 0;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    // albedo texture SRV table (t0).
    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[4].DescriptorTable.pDescriptorRanges = srvTableAlbedo;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    // normal texture SRV table (t1).
    rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[5].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[5].DescriptorTable.pDescriptorRanges = srvTableNormal;
    rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // material texture 샘플링용 linear wrap sampler.
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
    dsDesc.StencilEnable = TRUE;
    dsDesc.StencilReadMask = 0xFF;
    dsDesc.StencilWriteMask = 0xFF;
    dsDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    dsDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
    dsDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    dsDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    dsDesc.BackFace = dsDesc.FrontFace;

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
    pso.NumRenderTargets = 5;
    pso.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;  // Position
    pso.RTVFormats[1] = DXGI_FORMAT_R16G16B16A16_FLOAT;  // Normal
    pso.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;     // Albedo
    pso.RTVFormats[3] = DXGI_FORMAT_R8G8B8A8_UNORM;     // Material
    pso.RTVFormats[4] = DXGI_FORMAT_R32_FLOAT;          // Depth
    pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pso.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_GBufferPipelineState)));
}

void RenderSystem::CreateVelocityPipelineState() {
    auto device = m_RendererCore->GetDevice();

    // Velocity pass는 object/velocity/skinning CBV만 사용하고 색상 텍스처는 샘플링하지 않는다.
    D3D12_ROOT_PARAMETER rootParameters[3] = {};
    rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[0].Descriptor.ShaderRegister = 0;
    rootParameters[0].Descriptor.RegisterSpace = 0;
    rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[1].Descriptor.ShaderRegister = 1;
    rootParameters[1].Descriptor.RegisterSpace = 0;
    rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[2].Descriptor.ShaderRegister = 3;
    rootParameters[2].Descriptor.RegisterSpace = 0;
    rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = _countof(rootParameters);
    rootDesc.pParameters = rootParameters;
    rootDesc.NumStaticSamplers = 0;
    rootDesc.pStaticSamplers = nullptr;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig, err;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    ThrowIfFailed(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&m_VelocityRootSignature)));

    const std::wstring shaderPath = L"Renderer/Shaders/Velocity.hlsl";
    ComPtr<ID3DBlob> vs = d3dUtil::CompileShader(shaderPath, nullptr, "VS", "vs_5_0");
    ComPtr<ID3DBlob> ps = d3dUtil::CompileShader(shaderPath, nullptr, "PS", "ps_5_0");

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
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].LogicOpEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

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
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    dsDesc.StencilEnable = TRUE;
    dsDesc.StencilReadMask = 0xFF;
    dsDesc.StencilWriteMask = 0x00;
    dsDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
    dsDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    dsDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    dsDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    dsDesc.BackFace = dsDesc.FrontFace;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.InputLayout = { inputElements, _countof(inputElements) };
    pso.pRootSignature = m_VelocityRootSignature.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.RasterizerState = rastDesc;
    pso.BlendState = blendDesc;
    pso.DepthStencilState = dsDesc;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT;
    pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pso.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_VelocityPipelineState)));

    ComPtr<ID3DBlob> backgroundVs = d3dUtil::CompileShader(shaderPath, nullptr, "VSBackground", "vs_5_0");
    ComPtr<ID3DBlob> backgroundPs = d3dUtil::CompileShader(shaderPath, nullptr, "PSBackground", "ps_5_0");

    D3D12_DEPTH_STENCIL_DESC backgroundDsDesc = {};
    backgroundDsDesc.DepthEnable = FALSE;
    backgroundDsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    backgroundDsDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    backgroundDsDesc.StencilEnable = FALSE;

    D3D12_RASTERIZER_DESC backgroundRasterizer = rastDesc;
    backgroundRasterizer.CullMode = D3D12_CULL_MODE_NONE;
    backgroundRasterizer.DepthClipEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC backgroundPso = {};
    backgroundPso.InputLayout = { nullptr, 0 };
    backgroundPso.pRootSignature = m_VelocityRootSignature.Get();
    backgroundPso.VS = { backgroundVs->GetBufferPointer(), backgroundVs->GetBufferSize() };
    backgroundPso.PS = { backgroundPs->GetBufferPointer(), backgroundPs->GetBufferSize() };
    backgroundPso.RasterizerState = backgroundRasterizer;
    backgroundPso.BlendState = blendDesc;
    backgroundPso.DepthStencilState = backgroundDsDesc;
    backgroundPso.SampleMask = UINT_MAX;
    backgroundPso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    backgroundPso.NumRenderTargets = 1;
    backgroundPso.RTVFormats[0] = DXGI_FORMAT_R16G16_FLOAT;
    backgroundPso.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(
        &backgroundPso,
        IID_PPV_ARGS(&m_BackgroundVelocityPipelineState)));
}

void RenderSystem::CreateMotionVectorDebugPipelineState() {
    auto device = m_RendererCore->GetDevice();

    // Velocity texture 1장을 full-screen shader에서 읽어 HSV debug color로 변환한다.
    D3D12_DESCRIPTOR_RANGE srvTable = {};
    srvTable.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvTable.NumDescriptors = 1;
    srvTable.BaseShaderRegister = 0;
    srvTable.RegisterSpace = 0;
    srvTable.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER rootParameter = {};
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameter.DescriptorTable.NumDescriptorRanges = 1;
    rootParameter.DescriptorTable.pDescriptorRanges = &srvTable;
    rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplerDesc.ShaderRegister = 0;
    samplerDesc.RegisterSpace = 0;
    samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = &rootParameter;
    rootDesc.NumStaticSamplers = 1;
    rootDesc.pStaticSamplers = &samplerDesc;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig, err;
    ThrowIfFailed(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
    ThrowIfFailed(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
        IID_PPV_ARGS(&m_MotionVectorDebugRootSignature)));

    const std::wstring shaderPath = L"Renderer/Shaders/MotionVectorDebug.hlsl";
    ComPtr<ID3DBlob> vs = d3dUtil::CompileShader(shaderPath, nullptr, "VS", "vs_5_0");
    ComPtr<ID3DBlob> ps = d3dUtil::CompileShader(shaderPath, nullptr, "PS", "ps_5_0");

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    D3D12_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rastDesc.CullMode = D3D12_CULL_MODE_NONE;
    rastDesc.FrontCounterClockwise = FALSE;
    rastDesc.DepthClipEnable = FALSE;
    rastDesc.MultisampleEnable = FALSE;
    rastDesc.AntialiasedLineEnable = FALSE;
    rastDesc.ForcedSampleCount = 0;
    rastDesc.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

    D3D12_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = FALSE;
    dsDesc.StencilEnable = FALSE;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.InputLayout = { nullptr, 0 };
    pso.pRootSignature = m_MotionVectorDebugRootSignature.Get();
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

    ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_MotionVectorDebugPipelineState)));
}

void RenderSystem::CreateBackgroundResolvePipelineState()
{
    auto device = m_RendererCore->GetDevice();

    // Stencil이 0인 배경 픽셀만 그려 normal/depth MRT의 빈 값을 보정한다.
    D3D12_ROOT_PARAMETER rootParameter = {};
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameter.Descriptor.ShaderRegister = 0;
    rootParameter.Descriptor.RegisterSpace = 0;
    rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = 1;
    rootDesc.pParameters = &rootParameter;
    rootDesc.NumStaticSamplers = 0;
    rootDesc.pStaticSamplers = nullptr;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> sig;
    ComPtr<ID3DBlob> err;
    ThrowIfFailed(D3D12SerializeRootSignature(
        &rootDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        &sig,
        &err));
    ThrowIfFailed(device->CreateRootSignature(
        0,
        sig->GetBufferPointer(),
        sig->GetBufferSize(),
        IID_PPV_ARGS(&m_BackgroundResolveRootSignature)));

    const std::wstring shaderPath = L"Renderer/Shaders/BackgroundResolve.hlsl";
    ComPtr<ID3DBlob> vs = d3dUtil::CompileShader(shaderPath, nullptr, "VS", "vs_5_0");
    ComPtr<ID3DBlob> ps = d3dUtil::CompileShader(shaderPath, nullptr, "PS", "ps_5_0");

    D3D12_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = TRUE;
    for (int i = 0; i < 2; ++i) {
        blendDesc.RenderTarget[i].BlendEnable = FALSE;
        blendDesc.RenderTarget[i].LogicOpEnable = FALSE;
        blendDesc.RenderTarget[i].SrcBlend = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[i].DestBlend = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[i].BlendOp = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[i].SrcBlendAlpha = D3D12_BLEND_ONE;
        blendDesc.RenderTarget[i].DestBlendAlpha = D3D12_BLEND_ZERO;
        blendDesc.RenderTarget[i].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        blendDesc.RenderTarget[i].LogicOp = D3D12_LOGIC_OP_NOOP;
        blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    }

    D3D12_RASTERIZER_DESC rastDesc = {};
    rastDesc.FillMode = D3D12_FILL_MODE_SOLID;
    rastDesc.CullMode = D3D12_CULL_MODE_NONE;
    rastDesc.FrontCounterClockwise = FALSE;
    rastDesc.DepthClipEnable = TRUE;

    D3D12_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = FALSE;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    dsDesc.StencilEnable = TRUE;
    dsDesc.StencilReadMask = 0xFF;
    dsDesc.StencilWriteMask = 0x00;
    dsDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
    dsDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    dsDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    dsDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    dsDesc.BackFace = dsDesc.FrontFace;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
    pso.InputLayout = { nullptr, 0 };
    pso.pRootSignature = m_BackgroundResolveRootSignature.Get();
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.RasterizerState = rastDesc;
    pso.BlendState = blendDesc;
    pso.DepthStencilState = dsDesc;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 2;
    pso.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pso.RTVFormats[1] = DXGI_FORMAT_R32_FLOAT;
    pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    pso.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(
        &pso,
        IID_PPV_ARGS(&m_BackgroundResolvePipelineState)));
}

void RenderSystem::CreateLightingPipelineState() {
    auto device = m_RendererCore->GetDevice();

    // Root Signature: 패스 상수 CBV와 G-Buffer/SSGI SRV table을 바인딩한다.
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

    // s0: G-Buffer와 SSGI 텍스처를 점 샘플링한다.
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[0].ShaderRegister = 0;
    samplers[0].RegisterSpace = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1: ShadowMap PCF 비교 샘플러.
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
    rootDesc.NumStaticSamplers = 2;
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

    // Root Signature: pass constants, G-Buffer SRVs, SSGI output UAV.
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

    // Root Signature: position/normal/SSGI history SRV와 denoise output UAV를 분리 바인딩한다.
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
    rootDesc.NumParameters = 6;  // CBV + Position/Normal/SSGI/Previous SRV + Output UAV
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

    // Shadow depth texture는 DSV/SRV를 모두 만들 수 있도록 typeless format으로 생성한다.
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
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &optClear,
        IID_PPV_ARGS(&m_ShadowMap)));

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

    // Shadow pass는 고정 크기 shadow map 전체를 viewport/scissor로 사용한다.
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

    // ShadowMap SRV는 G-Buffer SRV heap의 t5 슬롯에 직접 생성한다.
    // DSV는 D24_UNORM_S8_UINT, SRV는 R24_UNORM_X8_TYPELESS 형식으로 같은 리소스를 해석한다.
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    // GBuffer SRV heap slot layout: Position=t0, Normal=t1, Albedo=t2, Material=t3, SSGI=t4, Shadow=t5.
    auto shadowSrvHandle = m_RendererCore->GetGBufferSRVHandle(5);
    device->CreateShaderResourceView(m_ShadowMap.Get(), &srvDesc, shadowSrvHandle);
}

void RenderSystem::CreateShadowPipelineState() {
    auto device = m_RendererCore->GetDevice();

    const std::wstring shaderPath = L"Renderer/Shaders/ShadowMap.hlsl";
    ComPtr<ID3DBlob> vs, ps;
    vs = d3dUtil::CompileShader(shaderPath, nullptr, "VS", "vs_5_0");
    ps = d3dUtil::CompileShader(shaderPath, nullptr, "PS", "ps_5_0");

    // Shadow pass도 G-Buffer와 같은 vertex layout을 사용한다.
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
    rastDesc.DepthBias = 100000;              // shadow acne를 줄이기 위한 depth bias.
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
    pso.pRootSignature = m_GBufferRootSignature.Get();         // G-Buffer root signature를 재사용한다.
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.RasterizerState = rastDesc;
    pso.BlendState = blendDesc;
    pso.DepthStencilState = dsDesc;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    pso.NumRenderTargets = 0;                                // shadow pass는 color render target이 없다.
    pso.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;           // CreateShadowResources의 DSV 형식과 맞춘다.
    pso.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_ShadowPipelineState)));
}


void RenderSystem::Update(float deltaTime) {
    m_TotalTime += deltaTime;
    m_DeltaTime = deltaTime;

    // 현재 예제 씬의 skinned entity에서 최종 bone transform을 가져와 모든 frame resource에 복사한다.
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

    // HLSL 상수 버퍼 레이아웃에 맞춰 bone matrix를 transpose한다.
    SkinningConstants skin = {};
    const uint32_t boneCount = (std::min)(
        static_cast<uint32_t>(skeleton->FinalBoneTransforms.size()),
        static_cast<uint32_t>(MAX_BONES)
        );

    for (size_t i = 0; i < boneCount; ++i) {
        XMMATRIX M = XMLoadFloat4x4(&skeleton->FinalBoneTransforms[i]);
        XMStoreFloat4x4(&skin.BoneTransforms[i], XMMatrixTranspose(M));
    }
    // 사용하지 않는 bone 슬롯은 identity로 채워 shader branch를 단순하게 유지한다.
    for (size_t i = boneCount; i < MAX_BONES; ++i) {
        DirectX::XMStoreFloat4x4(
            &skin.BoneTransforms[i],
            DirectX::XMMatrixIdentity()
        );
    }

    // 어느 frame index가 사용되더라도 같은 skinning 결과를 볼 수 있도록 전 프레임 버퍼에 업로드한다.
    for (UINT frame = 0; frame < FrameCount; ++frame) {
        memcpy(
            m_SkinningConstantBufferDataBegin[frame],
            &skin,
            sizeof(SkinningConstants)
        );
    }
}

void RenderSystem::Shutdown() {
    if (m_SkyRenderer) {
        m_SkyRenderer->Shutdown();
        m_SkyRenderer.reset();
    }

    m_EnvironmentManager.reset();

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

    if (m_WinMLStyleTransferSystem) {
        m_WinMLStyleTransferSystem->Shutdown();
        m_WinMLStyleTransferSystem.reset();
    }

    if (m_DirectSRUpscaler) {
        m_DirectSRUpscaler->Shutdown();
        m_DirectSRUpscaler.reset();
    }

    m_DirectSRMotionVectors.Reset();
    m_DirectSRMotionVectorRTVHeap.Reset();
    m_DirectSRMotionVectorSRVHeap.Reset();
    m_DirectSRMotionVectorRTV = {};

    if (m_RendererCore) {
        m_RendererCore->Shutdown();
    }
}

void RenderSystem::RegisterEntity(Entity* entity) {
    if (entity->HasComponent<MeshComponent>() && entity->HasComponent<TransformComponent>()) {
        m_RenderableEntities.push_back(entity);

        // 생산자 스레드는 엔티티 정보를 큐에 넣고, 렌더 스레드는 ProcessRenderCommands()에서 적용한다.
        RenderCommand command;
        command.Type = RenderCommandType::RegisterEntity;
        command.Id = entity->GetID();
        command.Transform = entity->GetComponent<TransformComponent>();
        command.Mesh = entity->GetComponent<MeshComponent>();
        command.Material = entity->GetComponent<MaterialComponent>();
        command.Active = entity->IsActive();
        m_RenderCommandQueue.Push(std::move(command));

        auto* meshComp = entity->GetComponent<MeshComponent>();
        if (meshComp && meshComp->isLoaded && !meshComp->vertexBuffer) {
            auto device = m_RendererCore->GetDevice();

            // Vertex buffer 생성.
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

            // Index buffer 생성.
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

    if (entity) {
        // RenderProxy 제거는 렌더 스레드에서 처리하도록 큐에 넣는다.
        RenderCommand command;
        command.Type = RenderCommandType::UnregisterEntity;
        command.Id = entity->GetID();
        m_RenderCommandQueue.Push(std::move(command));
    }
}

void RenderSystem::SetActiveSky(Entity* skyEntity)
{
    if (m_EnvironmentManager) {
        m_EnvironmentManager->SetActiveSky(skyEntity);
    }
}

void RenderSystem::Render() {
    // 렌더 스레드 프레임 순서:
    // 1) 큐에 쌓인 렌더 명령 적용
    // 2) physics/game snapshot으로 프레임 로컬 상태 생성
    // 3) 렌더 스레드가 소유한 데이터로 GPU 패스 제출
    ProcessRenderCommands();
    RefreshRenderSnapshot();

    m_RendererCore->BeginFrame();

    const UINT frameIndex = m_RendererCore->GetFrameIndex();
    UpdatePassConstants(frameIndex);

    RenderShadowPass(frameIndex);

    // G-Buffer와 motion vector는 모든 표시 모드에서 먼저 생성한다.
    RenderGBufferPass(frameIndex);
    RenderVelocityPass(frameIndex);

    RenderMode renderMode = RenderMode::Composite;
    {
        std::lock_guard<std::mutex> lock(m_SettingsMutex);
        renderMode = m_RenderMode;
    }
    // MotionVector 디버그 모드는 후속 조명/후처리를 건너뛰고 velocity texture만 백버퍼에 표시한다.
    if (renderMode == RenderMode::MotionVector) {
        RenderMotionVectorVisualizationPass();
        m_RendererCore->EndFrame();
        m_RendererCore->Present();
        return;
    }

    RenderBackgroundResolvePass(frameIndex);

    // SSGI compute pass.
    RenderSSGIPass(frameIndex);

    // SSGI denoise/temporal pass.
    RenderSSGIDenoisePass(frameIndex);


    CopySSGIToPrevious(frameIndex);

    // LightingBuffer에 조명과 sky를 합성한다.
    RenderLightingPass(frameIndex);
    RenderSkyPass(frameIndex);

    // StyleTransfer가 켜져 있으면 ONNX 후처리 결과를 DirectSR로 업스케일한다.
    bool styleTransferEnabled = false;
    {
        std::lock_guard<std::mutex> lock(m_SettingsMutex);
        styleTransferEnabled = m_IsStyleTransferEnabled;
    }

    if (styleTransferEnabled &&
        m_WinMLStyleTransferSystem &&
        m_WinMLStyleTransferSystem->Execute()) {
        ID3D12Resource* styleOutput = m_WinMLStyleTransferSystem->GetOutputTexture();
        if (!TryUpscaleStyleTransferOutput(styleOutput)) {
            if (styleOutput) {
                const D3D12_RESOURCE_DESC styleOutputDesc = styleOutput->GetDesc();
                if (styleOutputDesc.Width == m_Width && styleOutputDesc.Height == m_Height) {
                    CopyFrameToBackBuffer(styleOutput);
                }
                else {
                    DebugLogDirectSR(
                        "DirectSR upscale unavailable; copying lighting buffer because style output is not back-buffer sized: source="
                        + DirectSRTextureDesc(styleOutput)
                        + ", target="
                        + std::to_string(m_Width)
                        + "x"
                        + std::to_string(m_Height));
                    CopyFrameToBackBuffer(m_RendererCore->GetLightingBuffer());
                }
            }
            else {
                CopyFrameToBackBuffer(m_RendererCore->GetLightingBuffer());
            }
        }
    }
    else {
        CopyFrameToBackBuffer(m_RendererCore->GetLightingBuffer());
    }

    m_RendererCore->EndFrame();
    m_RendererCore->Present();
}

void RenderSystem::RenderShadowPass(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();

    // Shadow map 크기에 맞춘 viewport/scissor를 설정한다.
    commandList->RSSetViewports(1, &m_ShadowViewport);
    commandList->RSSetScissorRects(1, &m_ShadowScissorRect);

    // Shadow map을 depth write 상태로 전환한다.
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_ShadowMap.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_GENERIC_READ;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    commandList->ClearDepthStencilView(m_ShadowDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    commandList->OMSetRenderTargets(0, nullptr, FALSE, &m_ShadowDsv);

    commandList->SetPipelineState(m_ShadowPipelineState.Get());
    commandList->SetGraphicsRootSignature(m_GBufferRootSignature.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    int objectIndex = 0;
    for (const RenderProxy& proxy : m_RenderProxies) {
        if (proxy.Active) {
            // Shadow shader는 root signature를 공유하므로 기존 draw 바인딩 경로를 재사용한다.
            RenderProxyItem(proxy, frameIndex, objectIndex, FindSnapshotWorld(proxy.Id));
            ++objectIndex;
        }
    }

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    commandList->ResourceBarrier(1, &barrier);
}


void RenderSystem::RenderGBufferPass(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();
    auto device = m_RendererCore->GetDevice();

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

    // G-Buffer MRT를 render target 상태로 전환한다.
    D3D12_RESOURCE_BARRIER barriers[5] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = m_RendererCore->GetGBufferPosition();
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = m_RendererCore->GetGBufferNormal();
    barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[2].Transition.pResource = m_RendererCore->GetGBufferAlbedo();
    barriers[3].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[3].Transition.pResource = m_RendererCore->GetGBufferMaterial();
    barriers[4].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[4].Transition.pResource = m_RendererCore->GetGBufferDepth();
    
    // 첫 프레임은 resource 생성 상태가 이미 render target이므로 barrier를 생략한다.
    if (!m_IsFirstGBufferFrame) {
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[2].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[3].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[3].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[4].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[4].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        commandList->ResourceBarrier(5, barriers);
    }
    m_IsFirstGBufferFrame = false;

    // Position/Normal/Albedo/Material/ViewDepth MRT를 동시에 바인딩한다.
    D3D12_CPU_DESCRIPTOR_HANDLE gbufferRTVs[5] = {
        m_RendererCore->GetGBufferRTVHandle(0),
        m_RendererCore->GetGBufferRTVHandle(1),
        m_RendererCore->GetGBufferRTVHandle(2),
        m_RendererCore->GetGBufferRTVHandle(3),
        m_RendererCore->GetGBufferRTVHandle(4)
    };
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_RendererCore->GetDSVHeap()->GetCPUDescriptorHandleForHeapStart();

    // G-Buffer와 depth/stencil을 프레임마다 초기화한다.
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    commandList->ClearRenderTargetView(gbufferRTVs[0], clearColor, 0, nullptr);
    commandList->ClearRenderTargetView(gbufferRTVs[1], clearColor, 0, nullptr);
    commandList->ClearRenderTargetView(gbufferRTVs[2], clearColor, 0, nullptr);
    commandList->ClearRenderTargetView(gbufferRTVs[3], clearColor, 0, nullptr);
    commandList->ClearRenderTargetView(gbufferRTVs[4], clearColor, 0, nullptr);
    commandList->ClearDepthStencilView(
        dsvHandle,
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f,
        0,
        0,
        nullptr);

    commandList->OMSetRenderTargets(5, gbufferRTVs, FALSE, &dsvHandle);

    // G-Buffer PSO로 모든 렌더 proxy를 그린다.
    commandList->SetPipelineState(m_GBufferPipelineState.Get());
    commandList->SetGraphicsRootSignature(m_GBufferRootSignature.Get());
    commandList->OMSetStencilRef(1);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Render proxy 목록은 렌더 스레드가 소유하므로 draw 중 producer queue를 만지지 않는다.
    int objectIndex = 0;
    for (const RenderProxy& proxy : m_RenderProxies) {
        if (proxy.Active) {
            RenderProxyItem(proxy, frameIndex, objectIndex, FindSnapshotWorld(proxy.Id));
            objectIndex++;
        }
    }

    // 후속 SSGI/Lighting 패스가 읽을 수 있도록 G-Buffer를 SRV 상태로 전환한다.
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

    barriers[4].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[4].Transition.pResource = m_RendererCore->GetGBufferDepth();
    barriers[4].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[4].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    
    commandList->ResourceBarrier(5, barriers);
}

void RenderSystem::RenderBackgroundResolvePass(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();

    // G-Buffer에서 geometry가 없던 배경 픽셀의 normal/depth를 sky 방향 기준으로 채운다.
    D3D12_RESOURCE_BARRIER toRenderTarget[2] = {};
    toRenderTarget[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRenderTarget[0].Transition.pResource = m_RendererCore->GetGBufferNormal();
    toRenderTarget[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toRenderTarget[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRenderTarget[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toRenderTarget[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRenderTarget[1].Transition.pResource = m_RendererCore->GetGBufferDepth();
    toRenderTarget[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toRenderTarget[1].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRenderTarget[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(2, toRenderTarget);

    D3D12_CPU_DESCRIPTOR_HANDLE resolveRTVs[2] = {
        m_RendererCore->GetGBufferRTVHandle(1),
        m_RendererCore->GetGBufferRTVHandle(4)
    };
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_RendererCore->GetDSVHeap()->GetCPUDescriptorHandleForHeapStart();

    commandList->OMSetRenderTargets(2, resolveRTVs, FALSE, &dsvHandle);
    commandList->SetPipelineState(m_BackgroundResolvePipelineState.Get());
    commandList->SetGraphicsRootSignature(m_BackgroundResolveRootSignature.Get());
    commandList->OMSetStencilRef(0);

    D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = m_PassConstantBuffers[frameIndex]->GetGPUVirtualAddress();
    commandList->SetGraphicsRootConstantBufferView(0, passCBAddress);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);

    D3D12_RESOURCE_BARRIER toShaderResource[2] = {};
    toShaderResource[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toShaderResource[0].Transition.pResource = m_RendererCore->GetGBufferNormal();
    toShaderResource[0].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toShaderResource[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toShaderResource[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toShaderResource[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toShaderResource[1].Transition.pResource = m_RendererCore->GetGBufferDepth();
    toShaderResource[1].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toShaderResource[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toShaderResource[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(2, toShaderResource);
}

void RenderSystem::RenderLightingPass(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();
    // Lighting 결과는 백버퍼가 아니라 별도 LightingBuffer에 먼저 기록한다.
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_RendererCore->GetLightingRTVHandle();

    if (!m_IsFirstLightingFrame) {
        D3D12_RESOURCE_BARRIER toRenderTarget = {};
        toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toRenderTarget.Transition.pResource = m_RendererCore->GetLightingBuffer();
        toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &toRenderTarget);
    }

    const float clearColor[] = { 0.2f, 0.2f, 0.2f, 1.0f };
    commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
    commandList->SetPipelineState(m_LightingPipelineState.Get());
    commandList->SetGraphicsRootSignature(m_LightingRootSignature.Get());
    D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = m_PassConstantBuffers[frameIndex]->GetGPUVirtualAddress();
    commandList->SetGraphicsRootConstantBufferView(0, passCBAddress);
    ID3D12DescriptorHeap* srvHeaps[] = { m_RendererCore->GetGBufferSRVHeap() };
    commandList->SetDescriptorHeaps(1, srvHeaps);
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = m_RendererCore->GetGBufferSRVHeap()->GetGPUDescriptorHandleForHeapStart();
    commandList->SetGraphicsRootDescriptorTable(1, srvHandle);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);

    D3D12_RESOURCE_BARRIER toCopySource = {};
    toCopySource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopySource.Transition.pResource = m_RendererCore->GetLightingBuffer();
    toCopySource.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toCopySource.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toCopySource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &toCopySource);

    m_IsFirstLightingFrame = false;
}

void RenderSystem::CopyFrameToBackBuffer(ID3D12Resource* sourceTexture) {
    if (!sourceTexture) {
        return;
    }

    // 최종 출력은 CopyResource로 백버퍼에 복사한 뒤 present로 넘긴다.
    auto* commandList = m_RendererCore->GetCommandList();
    auto* backBuffer = m_RendererCore->GetCurrentBackBuffer();

    D3D12_RESOURCE_BARRIER toCopyDest = {};
    toCopyDest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopyDest.Transition.pResource = backBuffer;
    toCopyDest.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toCopyDest.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    toCopyDest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &toCopyDest);

    commandList->CopyResource(backBuffer, sourceTexture);

    D3D12_RESOURCE_BARRIER toRenderTarget = {};
    toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRenderTarget.Transition.pResource = backBuffer;
    toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &toRenderTarget);
}

bool RenderSystem::TryUpscaleStyleTransferOutput(ID3D12Resource* styleOutputTexture) {
    if (!m_DirectSRUpscaler) {
        DebugLogDirectSR("DirectSR upscale skipped: upscaler is not initialized.");
        return false;
    }

    if (!styleOutputTexture) {
        DebugLogDirectSR("DirectSR upscale skipped: style transfer output texture is null.");
        return false;
    }

    if (!m_DirectSRMotionVectors) {
        DebugLogDirectSR("DirectSR upscale skipped: motion vector texture is not available.");
        return false;
    }

    ID3D12Resource* depthTexture = m_RendererCore->GetGBufferDepth();
    if (!depthTexture) {
        DebugLogDirectSR("DirectSR upscale skipped: depth texture is not available.");
        return false;
    }

    DebugLogDirectSR(
        "DirectSR upscale requested: source="
        + DirectSRTextureDesc(styleOutputTexture)
        + ", target="
        + std::to_string(m_Width)
        + "x"
        + std::to_string(m_Height));

    auto* commandList = m_RendererCore->GetCommandList();

    // DirectSR는 style output과 depth를 compute shader 입력으로 읽는다.
    D3D12_RESOURCE_BARRIER toDirectSRInputs[2] = {};
    toDirectSRInputs[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toDirectSRInputs[0].Transition.pResource = styleOutputTexture;
    toDirectSRInputs[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toDirectSRInputs[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    toDirectSRInputs[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    toDirectSRInputs[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toDirectSRInputs[1].Transition.pResource = depthTexture;
    toDirectSRInputs[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toDirectSRInputs[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    toDirectSRInputs[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(_countof(toDirectSRInputs), toDirectSRInputs);

    // DirectSR wrapper가 별도 실행을 수행하므로 입력 상태 전환 command list를 먼저 끝낸다.
    m_RendererCore->ExecuteCommandListAndWait();
    m_RendererCore->ResetCommandList();

    // DirectSR에 color/depth/motion vector와 카메라 정보를 전달한다.
    DirectSRUpscaler::UpscaleDesc upscaleDesc = {};
    upscaleDesc.SourceColorTexture = styleOutputTexture;
    upscaleDesc.SourceDepthTexture = depthTexture;
    upscaleDesc.MotionVectorsTexture = m_DirectSRMotionVectors.Get();
    upscaleDesc.SourceColorRegion = { 0, 0, static_cast<LONG>(m_StyleOutputWidth), static_cast<LONG>(m_StyleOutputHeight) };
    upscaleDesc.SourceDepthRegion = { 0, 0, static_cast<LONG>(m_StyleOutputWidth), static_cast<LONG>(m_StyleOutputHeight) };
    upscaleDesc.MotionVectorsRegion = { 0, 0, static_cast<LONG>(m_Width), static_cast<LONG>(m_Height) };
    upscaleDesc.TargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    upscaleDesc.SourceColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    upscaleDesc.SourceDepthFormat = DXGI_FORMAT_R32_FLOAT;
    upscaleDesc.MotionVectorScale = { 1.0f, 1.0f };
    upscaleDesc.TimeDeltaInSeconds = m_DeltaTime;
    upscaleDesc.ResetHistory = m_DirectSRResetHistory;
    upscaleDesc.Sharpness = 0.5f;

    if (m_Engine && m_Engine->GetMainCamera()) {
        CameraComponent* camera = m_Engine->GetMainCamera()->GetComponent<CameraComponent>();
        if (camera) {
            upscaleDesc.CameraFovAngleVert = camera->Fov;
        }
    }

    HRESULT hr = m_DirectSRUpscaler->Upscale(m_Width, m_Height, upscaleDesc);
    commandList = m_RendererCore->GetCommandList();

    if (FAILED(hr)) {
#if defined(_DEBUG)
        OutputDebugStringA(("DirectSR upscale failed: " + m_DirectSRUpscaler->GetLastError() + "\n").c_str());
#endif
        // 실패 시 다음 fallback copy 경로가 사용할 수 있도록 입력 리소스 상태를 원복한다.
        D3D12_RESOURCE_BARRIER restoreInputs[2] = {};
        restoreInputs[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        restoreInputs[0].Transition.pResource = styleOutputTexture;
        restoreInputs[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        restoreInputs[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        restoreInputs[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        restoreInputs[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        restoreInputs[1].Transition.pResource = depthTexture;
        restoreInputs[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        restoreInputs[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        restoreInputs[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(_countof(restoreInputs), restoreInputs);
        m_DirectSRResetHistory = true;
        return false;
    }

    ID3D12Resource* upscaledTexture = m_DirectSRUpscaler->GetOutputTexture();
    if (!upscaledTexture) {
        DebugLogDirectSR("DirectSR upscale failed: output texture was not created.");
        D3D12_RESOURCE_BARRIER restoreInputs[2] = {};
        restoreInputs[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        restoreInputs[0].Transition.pResource = styleOutputTexture;
        restoreInputs[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        restoreInputs[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        restoreInputs[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        restoreInputs[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        restoreInputs[1].Transition.pResource = depthTexture;
        restoreInputs[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        restoreInputs[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        restoreInputs[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(_countof(restoreInputs), restoreInputs);
        m_DirectSRResetHistory = true;
        return false;
    }

    // 성공 시 출력은 copy source로, 입력들은 원래 렌더 파이프라인 상태로 돌린다.
    D3D12_RESOURCE_BARRIER afterUpscale[3] = {};
    afterUpscale[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    afterUpscale[0].Transition.pResource = upscaledTexture;
    afterUpscale[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    afterUpscale[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    afterUpscale[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    afterUpscale[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    afterUpscale[1].Transition.pResource = styleOutputTexture;
    afterUpscale[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    afterUpscale[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    afterUpscale[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    afterUpscale[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    afterUpscale[2].Transition.pResource = depthTexture;
    afterUpscale[2].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    afterUpscale[2].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    afterUpscale[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(_countof(afterUpscale), afterUpscale);

    CopyFrameToBackBuffer(upscaledTexture);
    DebugLogDirectSR("DirectSR upscale output copied to back buffer: output=" + DirectSRTextureDesc(upscaledTexture));

    D3D12_RESOURCE_BARRIER restoreOutput = {};
    restoreOutput.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    restoreOutput.Transition.pResource = upscaledTexture;
    restoreOutput.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    restoreOutput.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    restoreOutput.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &restoreOutput);

    m_DirectSRResetHistory = false;
    return true;
}

void RenderSystem::CreateDirectSRResources() {
    auto* device = m_RendererCore->GetDevice();

    // Velocity pass가 기록할 RTV heap.
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_DirectSRMotionVectorRTVHeap)))) {
        DebugLogDirectSR("DirectSR resource creation failed: motion vector RTV heap could not be created.");
        return;
    }
    m_DirectSRMotionVectorRTV = m_DirectSRMotionVectorRTVHeap->GetCPUDescriptorHandleForHeapStart();

    // MotionVector debug shader가 같은 texture를 읽을 수 있게 shader-visible SRV heap도 만든다.
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 1;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_DirectSRMotionVectorSRVHeap)))) {
        DebugLogDirectSR("DirectSR resource creation failed: motion vector SRV heap could not be created.");
        m_DirectSRMotionVectorRTVHeap.Reset();
        m_DirectSRMotionVectorRTV = {};
        return;
    }

    // DirectSR는 motion vector를 pixel 단위 R16G16_FLOAT 값으로 받는다.
    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = m_Width;
    textureDesc.Height = m_Height;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R16G16_FLOAT;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 0.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        &clearValue,
        IID_PPV_ARGS(&m_DirectSRMotionVectors));
    if (FAILED(hr)) {
        DebugLogDirectSR("DirectSR resource creation failed: motion vector texture could not be created.");
        m_DirectSRMotionVectorSRVHeap.Reset();
        m_DirectSRMotionVectorRTVHeap.Reset();
        m_DirectSRMotionVectorRTV = {};
        return;
    }

    device->CreateRenderTargetView(m_DirectSRMotionVectors.Get(), nullptr, m_DirectSRMotionVectorRTV);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(
        m_DirectSRMotionVectors.Get(),
        &srvDesc,
        m_DirectSRMotionVectorSRVHeap->GetCPUDescriptorHandleForHeapStart());
}

void RenderSystem::RenderVelocityPass(UINT frameIndex) {
    if (!m_DirectSRMotionVectors || m_DirectSRMotionVectorRTV.ptr == 0 || !m_VelocityPipelineState) {
        return;
    }

    auto* commandList = m_RendererCore->GetCommandList();

    // DirectSR input 상태에서 velocity render target 상태로 전환한다.
    D3D12_RESOURCE_BARRIER toRenderTarget = {};
    toRenderTarget.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toRenderTarget.Transition.pResource = m_DirectSRMotionVectors.Get();
    toRenderTarget.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    toRenderTarget.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toRenderTarget.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &toRenderTarget);

    // 움직임이 없는 영역은 0 velocity로 초기화한다.
    const float zero[] = { 0.0f, 0.0f, 0.0f, 0.0f };
    commandList->ClearRenderTargetView(m_DirectSRMotionVectorRTV, zero, 0, nullptr);

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(m_Width);
    viewport.Height = static_cast<float>(m_Height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor = {};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = static_cast<LONG>(m_Width);
    scissor.bottom = static_cast<LONG>(m_Height);

    RenderBackgroundVelocity(frameIndex);

    // Stencil 1인 geometry 픽셀만 motion vector를 기록한다.
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_RendererCore->GetDSVHeap()->GetCPUDescriptorHandleForHeapStart();
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    commandList->OMSetRenderTargets(1, &m_DirectSRMotionVectorRTV, FALSE, &dsvHandle);
    commandList->SetPipelineState(m_VelocityPipelineState.Get());
    commandList->SetGraphicsRootSignature(m_VelocityRootSignature.Get());
    commandList->OMSetStencilRef(1);
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_GPU_VIRTUAL_ADDRESS velocityPassCBAddress =
        m_VelocityPassConstantBuffers[frameIndex]->GetGPUVirtualAddress();
    commandList->SetGraphicsRootConstantBufferView(1, velocityPassCBAddress);

    int objectIndex = 0;
    for (const RenderProxy& proxy : m_RenderProxies) {
        if (proxy.Active) {
            RenderVelocityProxyItem(proxy, frameIndex, objectIndex, FindSnapshotWorld(proxy.Id));
            ++objectIndex;
        }
    }

    // DirectSR가 이후 compute 입력으로 읽을 수 있게 NON_PIXEL_SHADER_RESOURCE 상태로 되돌린다.
    D3D12_RESOURCE_BARRIER toDirectSRInput = {};
    toDirectSRInput.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toDirectSRInput.Transition.pResource = m_DirectSRMotionVectors.Get();
    toDirectSRInput.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    toDirectSRInput.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    toDirectSRInput.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &toDirectSRInput);
}

void RenderSystem::RenderBackgroundVelocity(UINT frameIndex)
{
    if (!m_BackgroundVelocityPipelineState) {
        return;
    }

    auto* commandList = m_RendererCore->GetCommandList();

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(m_Width);
    viewport.Height = static_cast<float>(m_Height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor = {};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = static_cast<LONG>(m_Width);
    scissor.bottom = static_cast<LONG>(m_Height);

    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    commandList->OMSetRenderTargets(1, &m_DirectSRMotionVectorRTV, FALSE, nullptr);
    commandList->SetPipelineState(m_BackgroundVelocityPipelineState.Get());
    commandList->SetGraphicsRootSignature(m_VelocityRootSignature.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    D3D12_GPU_VIRTUAL_ADDRESS velocityPassCBAddress =
        m_VelocityPassConstantBuffers[frameIndex]->GetGPUVirtualAddress();
    commandList->SetGraphicsRootConstantBufferView(1, velocityPassCBAddress);

    commandList->DrawInstanced(3, 1, 0, 0);
}

void RenderSystem::RenderMotionVectorVisualizationPass() {
    if (!m_DirectSRMotionVectors || !m_DirectSRMotionVectorSRVHeap || !m_MotionVectorDebugPipelineState) {
        return;
    }

    auto* commandList = m_RendererCore->GetCommandList();

    // Debug pass는 pixel shader에서 velocity texture를 샘플링한다.
    D3D12_RESOURCE_BARRIER toPixelShaderResource = {};
    toPixelShaderResource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toPixelShaderResource.Transition.pResource = m_DirectSRMotionVectors.Get();
    toPixelShaderResource.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    toPixelShaderResource.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toPixelShaderResource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &toPixelShaderResource);

    D3D12_VIEWPORT viewport = {};
    viewport.TopLeftX = 0.0f;
    viewport.TopLeftY = 0.0f;
    viewport.Width = static_cast<float>(m_Width);
    viewport.Height = static_cast<float>(m_Height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissor = {};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = static_cast<LONG>(m_Width);
    scissor.bottom = static_cast<LONG>(m_Height);

    // Back buffer에 full-screen triangle을 그려 velocity 방향/크기를 색으로 표시한다.
    D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV = m_RendererCore->GetCurrentBackBufferRTV();
    const float clearColor[] = { 0.02f, 0.02f, 0.025f, 1.0f };
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);
    commandList->ClearRenderTargetView(backBufferRTV, clearColor, 0, nullptr);
    commandList->OMSetRenderTargets(1, &backBufferRTV, FALSE, nullptr);
    commandList->SetPipelineState(m_MotionVectorDebugPipelineState.Get());
    commandList->SetGraphicsRootSignature(m_MotionVectorDebugRootSignature.Get());

    ID3D12DescriptorHeap* heaps[] = { m_DirectSRMotionVectorSRVHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetGraphicsRootDescriptorTable(
        0,
        m_DirectSRMotionVectorSRVHeap->GetGPUDescriptorHandleForHeapStart());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList->DrawInstanced(3, 1, 0, 0);

    // 디버그 표시가 끝나면 DirectSR input 상태로 복원한다.
    D3D12_RESOURCE_BARRIER toDirectSRInput = {};
    toDirectSRInput.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toDirectSRInput.Transition.pResource = m_DirectSRMotionVectors.Get();
    toDirectSRInput.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toDirectSRInput.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    toDirectSRInput.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &toDirectSRInput);
}

void RenderSystem::RenderSkyPass(UINT frameIndex)
{
    if (!m_EnvironmentManager || !m_SkyRenderer) {
        return;
    }

    Entity* skyEntity = m_EnvironmentManager->GetActiveSkyEntity();
    if (!skyEntity || !skyEntity->IsActive()) {
        return;
    }

    SkyComponent* sky = m_EnvironmentManager->GetActiveSkyComponent();
    if (!sky || !sky->Visible || sky->Type != SkyType::Cubemap) {
        return;
    }

    TextureInfo* cubemap = m_EnvironmentManager->GetActiveSkyCubemap();
    if (!cubemap) {
        return;
    }

    if (m_CurrentRenderSnapshot.HasCamera()) {
        m_SkyRenderer->Render(
            m_RendererCore->GetCommandList(),
            m_CurrentRenderSnapshot.GetCamera(),
            *sky,
            *cubemap,
            frameIndex);
        return;
    }

    Entity* cameraEntity = m_Engine->GetMainCamera();
    if (!cameraEntity) {
        return;
    }

    CameraComponent* camera = cameraEntity->GetComponent<CameraComponent>();
    if (!camera) {
        return;
    }

    m_SkyRenderer->Render(
        m_RendererCore->GetCommandList(),
        *camera,
        *sky,
        *cubemap,
        frameIndex);
}

void RenderSystem::ToggleRenderMode() {
    std::lock_guard<std::mutex> lock(m_SettingsMutex);

    switch (m_RenderMode) {
    case RenderMode::Composite:
        m_RenderMode = RenderMode::Lighting;
        break;
    case RenderMode::Lighting:
        m_RenderMode = RenderMode::SSGI;
        break;
    case RenderMode::SSGI:
        m_RenderMode = RenderMode::MotionVector;
        break;
    case RenderMode::MotionVector:
        m_RenderMode = RenderMode::Composite;
        break;
    }
}

void RenderSystem::ToggleStyleTransfer() {
    std::lock_guard<std::mutex> lock(m_SettingsMutex);
    m_IsStyleTransferEnabled = !m_IsStyleTransferEnabled;
}

RenderMode RenderSystem::GetRenderMode() const {
    std::lock_guard<std::mutex> lock(m_SettingsMutex);
    return m_RenderMode;
}

void RenderSystem::RenderSSGIPass(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();
    auto device = m_RendererCore->GetDevice();

    // SSGI 버퍼를 compute shader가 쓸 수 있는 UAV 상태로 전환한다.
    D3D12_RESOURCE_BARRIER barrier = {};
    if (!m_IsFirstSSGIFrame) {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_RendererCore->GetSSGIBuffer();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);
    }

    // SSGI compute PSO와 root signature를 바인딩한다.
    commandList->SetPipelineState(m_SSGIPipelineState.Get());
    commandList->SetComputeRootSignature(m_SSGIRootSignature.Get());

    // 카메라/해상도/시간 정보가 들어 있는 pass constant buffer를 바인딩한다.
    D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = m_PassConstantBuffers[frameIndex]->GetGPUVirtualAddress();
    commandList->SetComputeRootConstantBufferView(0, passCBAddress);

    ID3D12DescriptorHeap* heaps[] = { m_RendererCore->GetGBufferSRVHeap() };
    commandList->SetDescriptorHeaps(1, heaps);

    // Position/Normal/Albedo/Material SRV table을 한 번에 바인딩한다.
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = m_RendererCore->GetGBufferSRVHeap()->GetGPUDescriptorHandleForHeapStart();
    commandList->SetComputeRootDescriptorTable(1, srvHandle);

    // SSGI 결과는 G-Buffer SRV heap에 같이 들어 있는 UAV 슬롯에 기록한다.
    D3D12_GPU_DESCRIPTOR_HANDLE uavHandle = m_RendererCore->GetSSGIUAVHandleFromGBufferHeap();
    commandList->SetComputeRootDescriptorTable(2, uavHandle);

    // Dispatch는 8x8 thread group 기준으로 화면 전체를 덮는다.
    uint32_t width = m_RendererCore->GetWidth();
    uint32_t height = m_RendererCore->GetHeight();
    uint32_t dispatchX = (width + 7) / 8;
    uint32_t dispatchY = (height + 7) / 8;
    commandList->Dispatch(dispatchX, dispatchY, 1);

    
    m_IsFirstSSGIFrame = false;
}

void RenderSystem::RenderSSGIDenoisePass(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();
    auto device = m_RendererCore->GetDevice();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_RendererCore->GetSSGIBuffer();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;  // SSGI pass에서 UAV로 쓴 상태.
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;  // denoise pass에서 SRV로 읽는다.
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // Denoise compute PSO와 root signature를 바인딩한다.
    commandList->SetPipelineState(m_SSGIDenoisePipelineState.Get());
    commandList->SetComputeRootSignature(m_SSGIDenoiseRootSignature.Get());

    // Denoise shader도 화면 크기와 카메라 정보가 필요하다.
    D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = m_PassConstantBuffers[frameIndex]->GetGPUVirtualAddress();
    commandList->SetComputeRootConstantBufferView(0, passCBAddress);

    ID3D12DescriptorHeap* heaps[] = { m_RendererCore->GetGBufferSRVHeap() };
    commandList->SetDescriptorHeaps(1, heaps);

    // Position/Normal은 bilateral filter의 geometry-aware weight 계산에 사용한다.
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = m_RendererCore->GetGBufferSRVHeap()->GetGPUDescriptorHandleForHeapStart();
    
    // Position SRV (t0).
    commandList->SetComputeRootDescriptorTable(1, srvHandle);
    
    // Normal SRV (t1).
    D3D12_GPU_DESCRIPTOR_HANDLE normalHandle = srvHandle;
    normalHandle.ptr += m_RendererCore->GetGBufferSRVDescriptorSize();
    commandList->SetComputeRootDescriptorTable(2, normalHandle);
    
    // 현재 SSGI 결과와 이전 프레임 결과를 함께 읽어 temporal filter를 적용한다.
    D3D12_GPU_DESCRIPTOR_HANDLE ssgiSrvHandle = m_RendererCore->GetSSGISRVHandleFromGBufferHeap();
    commandList->SetComputeRootDescriptorTable(3, ssgiSrvHandle);
    
    D3D12_GPU_DESCRIPTOR_HANDLE ssgiPreviousSrvHandle = m_RendererCore->GetSSGIPreviousSRVHandleFromGBufferHeap();
    commandList->SetComputeRootDescriptorTable(4, ssgiPreviousSrvHandle);
    
    D3D12_GPU_DESCRIPTOR_HANDLE uavHandle = m_RendererCore->GetSSGIUAVHandleFromGBufferHeap();
    commandList->SetComputeRootDescriptorTable(5, uavHandle);

    // 같은 SSGI 텍스처를 다시 UAV로 바꿔 denoise 결과를 덮어쓴다.
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    commandList->ResourceBarrier(1, &barrier);

    // Dispatch는 8x8 thread group 기준으로 화면 전체를 덮는다.
    uint32_t width = m_RendererCore->GetWidth();
    uint32_t height = m_RendererCore->GetHeight();
    uint32_t dispatchX = (width + 7) / 8;
    uint32_t dispatchY = (height + 7) / 8;
    commandList->Dispatch(dispatchX, dispatchY, 1);

    // Lighting pass가 읽을 수 있도록 최종 SSGI 버퍼를 pixel shader SRV 상태로 복원한다.
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
}

void RenderSystem::CopySSGIToPrevious(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();
    
    // temporal denoise를 위해 현재 SSGI를 previous buffer로 복사한다.
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = m_RendererCore->GetSSGIBuffer();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = m_RendererCore->GetSSGIPreviousBuffer();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    
    commandList->ResourceBarrier(2, barriers);
    
    commandList->CopyResource(
        m_RendererCore->GetSSGIPreviousBuffer(),
        m_RendererCore->GetSSGIBuffer()
    );
    
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    
    commandList->ResourceBarrier(2, barriers);
}

void RenderSystem::UpdatePassConstants(UINT frameIndex) {
    PassConstants passConstants = {};
    
    // 렌더 스냅샷이 있으면 스냅샷 카메라를 우선 사용하고, 없으면 live CameraComponent로 fallback한다.
    XMMATRIX V = XMMatrixIdentity();
    XMMATRIX P = XMMatrixIdentity();

    if (m_CurrentRenderSnapshot.HasCamera()) {
        const CameraRenderState& camera = m_CurrentRenderSnapshot.GetCamera();
        V = XMLoadFloat4x4(&camera.View);
        P = XMLoadFloat4x4(&camera.Proj);
    }
    else {
    Entity* mainCamera = m_Engine->GetMainCamera();
    if (!mainCamera) {
        return;
    }    
    
    auto cameraComp = mainCamera->GetComponent<CameraComponent>();
    if (!cameraComp) return;

    V = XMLoadFloat4x4(&cameraComp->ViewMatrix);
    P = XMLoadFloat4x4(&cameraComp->ProjMatrix);
    }
    XMMATRIX VP = XMMatrixMultiply(V, P);
    // 현재 VP는 다음 프레임 velocity 계산에서 previous VP로 이동한다.
    XMStoreFloat4x4(&m_CurrentViewMatrix, V);
    XMStoreFloat4x4(&m_CurrentProjMatrix, P);
    XMStoreFloat4x4(&m_CurrentViewProjMatrix, VP);
    m_HasCurrentViewProjMatrix = true;
    XMMATRIX prevVP = m_HasPreviousViewProjMatrix ? XMLoadFloat4x4(&m_PreviousViewProjMatrix) : VP;
    XMMATRIX prevV = m_HasPreviousViewProjMatrix ? XMLoadFloat4x4(&m_PreviousViewMatrix) : V;
    XMMATRIX prevP = m_HasPreviousViewProjMatrix ? XMLoadFloat4x4(&m_PreviousProjMatrix) : P;
    
    XMStoreFloat4x4(&passConstants.gView, XMMatrixTranspose(V));
    XMStoreFloat4x4(&passConstants.gInvView, XMMatrixTranspose(XMMatrixInverse(nullptr, V)));
    XMStoreFloat4x4(&passConstants.gProj, XMMatrixTranspose(P));
    XMStoreFloat4x4(&passConstants.gInvProj, XMMatrixTranspose(XMMatrixInverse(nullptr, P)));
    XMStoreFloat4x4(&passConstants.gViewProj, XMMatrixTranspose(VP));
    XMStoreFloat4x4(&passConstants.gInvViewProj, XMMatrixTranspose(XMMatrixInverse(nullptr, VP)));
    
    // 예제 씬의 주 directional light 기준으로 shadow matrix를 구성한다.
    XMVECTOR lightDir = XMVector3Normalize(
        XMLoadFloat3(&passConstants.gLights[0].Direction)
    );

    if (XMVector3Less(XMVector3LengthSq(lightDir), XMVectorReplicate(0.001f)))
    {
        lightDir = XMVectorSet(0.577f, -0.577f, 0.577f, 0.0f);
    }

    XMVECTOR targetPos = XMVectorZero();

    const float lightDist = 50.0f; // shadow 카메라를 원점에서 충분히 떨어뜨린다.
    XMVECTOR lightPos = XMVectorMultiplyAdd(
        XMVectorReplicate(-lightDist),
        lightDir,
        targetPos
    );

    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, up);

    // 현재 씬 범위를 덮는 단순 orthographic shadow projection.
    float l = -50.0f, r = 50.0f;
    float b = -50.0f, t = 50.0f;
    float n = 1.0f, f = 150.0f;
    XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

    XMMATRIX lightViewProj = lightView * lightProj;

    XMStoreFloat4x4(&passConstants.gShadowView, XMMatrixTranspose(lightView));
    XMStoreFloat4x4(&passConstants.gShadowProj, XMMatrixTranspose(lightProj));
    XMStoreFloat4x4(&passConstants.gShadowViewProj, XMMatrixTranspose(lightViewProj));

    // NDC [-1, 1] 좌표를 shadow texture [0, 1] 좌표로 변환한다.
    XMMATRIX T(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f
    );

    XMMATRIX shadowTransform = lightViewProj * T;
    XMStoreFloat4x4(&passConstants.gShadowTransform,
        XMMatrixTranspose(shadowTransform));

    // Eye position은 view 행렬의 inverse translation에서 얻는다.
    XMMATRIX invV = XMMatrixInverse(nullptr, V);
    XMStoreFloat3(&passConstants.gEyePosW, invV.r[3]);
    
    passConstants.gRenderTargetSize = XMFLOAT2(static_cast<float>(m_Width), static_cast<float>(m_Height));
    passConstants.gInvRenderTargetSize = XMFLOAT2(1.0f / m_Width, 1.0f / m_Height);
    passConstants.gNearZ = 0.1f;
    passConstants.gFarZ = 100.0f;
    passConstants.gTotalTime = m_TotalTime;
    passConstants.gDeltaTime = m_DeltaTime;
    passConstants.gAmbientLight = m_AmbientLight;
    {
        std::lock_guard<std::mutex> lock(m_SettingsMutex);
        passConstants.gRenderMode = static_cast<int>(m_RenderMode);
    }
    passConstants.cbPerObjectPad3 = 0.0f;
    passConstants.cbPerObjectPad4 = XMFLOAT2(0.0f, 0.0f);
    
    // Lighting.hlsl은 4개의 directional light를 기대하므로 기본 방향광을 채운다.
    passConstants.gLights[0].Strength = XMFLOAT3(0.9f, 0.9f, 0.9f);
    passConstants.gLights[0].Direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
    passConstants.gLights[0].FalloffStart = 1.0f;
    passConstants.gLights[0].FalloffEnd = 1000.0f;
    passConstants.gLights[0].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
    passConstants.gLights[0].SpotPower = 1.0f;
    
    passConstants.gLights[1].Strength = XMFLOAT3(0.3f, 0.3f, 0.3f);
    passConstants.gLights[1].Direction = XMFLOAT3(-0.5f, -0.5f, -0.5f);
    passConstants.gLights[1].FalloffStart = 1.0f;
    passConstants.gLights[1].FalloffEnd = 10.0f;
    passConstants.gLights[1].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
    passConstants.gLights[1].SpotPower = 64.0f;
    
    passConstants.gLights[2].Strength = XMFLOAT3(0.2f, 0.2f, 0.2f);
    passConstants.gLights[2].Direction = XMFLOAT3(0.5f, -0.5f, 0.5f);
    passConstants.gLights[2].FalloffStart = 1.0f;
    passConstants.gLights[2].FalloffEnd = 10.0f;
    passConstants.gLights[2].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
    passConstants.gLights[2].SpotPower = 64.0f;
    
    passConstants.gLights[3].Strength = XMFLOAT3(0.4f, 0.4f, 0.4f);
    passConstants.gLights[3].Direction = XMFLOAT3(0.0f, 0.0f, -1.0f);
    passConstants.gLights[3].FalloffStart = 1.0f;
    passConstants.gLights[3].FalloffEnd = 1000.0f;
    passConstants.gLights[3].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
    passConstants.gLights[3].SpotPower = 1.0f;
    
    // 나머지 light 슬롯은 꺼 둔다.
    for (int i = 4; i < 16; ++i) {
        passConstants.gLights[i].Strength = XMFLOAT3(0.0f, 0.0f, 0.0f);
        passConstants.gLights[i].Direction = XMFLOAT3(0.0f, 0.0f, 0.0f);
        passConstants.gLights[i].FalloffStart = 0.0f;
        passConstants.gLights[i].FalloffEnd = 0.0f;
        passConstants.gLights[i].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
        passConstants.gLights[i].SpotPower = 0.0f;
    }



    // PassConstants와 VelocityPassConstants는 서로 다른 root signature에서 별도 CBV로 바인딩된다.
    memcpy(m_PassConstantBufferDataBegin[frameIndex], &passConstants, sizeof(PassConstants));

    VelocityPassConstants velocityConstants = {};
    XMStoreFloat4x4(&velocityConstants.gViewProj, XMMatrixTranspose(VP));
    XMStoreFloat4x4(&velocityConstants.gPrevViewProj, XMMatrixTranspose(prevVP));
    XMMATRIX viewNoTranslation = V;
    viewNoTranslation.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    XMMATRIX prevViewNoTranslation = prevV;
    prevViewNoTranslation.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    XMStoreFloat4x4(&velocityConstants.gInvViewNoTranslation,
        XMMatrixTranspose(XMMatrixInverse(nullptr, viewNoTranslation)));
    XMStoreFloat4x4(&velocityConstants.gInvProj, XMMatrixTranspose(XMMatrixInverse(nullptr, P)));
    XMStoreFloat4x4(&velocityConstants.gPrevViewNoTranslation, XMMatrixTranspose(prevViewNoTranslation));
    XMStoreFloat4x4(&velocityConstants.gPrevProj, XMMatrixTranspose(prevP));
    velocityConstants.gRenderTargetSize = XMFLOAT2(static_cast<float>(m_Width), static_cast<float>(m_Height));
    velocityConstants.gInvRenderTargetSize = XMFLOAT2(1.0f / m_Width, 1.0f / m_Height);
    memcpy(
        m_VelocityPassConstantBufferDataBegin[frameIndex],
        &velocityConstants,
        sizeof(VelocityPassConstants));
}

void RenderSystem::RenderEntity(Entity* entity, UINT frameIndex, int objectIndex) {
    RenderEntity(entity, frameIndex, objectIndex, FindSnapshotWorld(entity->GetID()));
}

void RenderSystem::RenderEntity(Entity* entity, UINT frameIndex, int objectIndex, const XMFLOAT4X4* snapshotWorld) {
    auto transform = entity->GetComponent<TransformComponent>();
    auto mesh = entity->GetComponent<MeshComponent>();
    auto material = entity->GetComponent<MaterialComponent>();

    if (!transform || !mesh || !material || !mesh->vertexBuffer) {
        return;
    }

    auto commandList = m_RendererCore->GetCommandList();

    ObjectConstants objConstants = {};
    XMMATRIX W = snapshotWorld ? XMLoadFloat4x4(snapshotWorld) : transform->GetWorldMatrix();
    const XMFLOAT4X4* previousSnapshotWorld = FindPreviousSnapshotWorld(entity->GetID());
    XMMATRIX prevW = previousSnapshotWorld ? XMLoadFloat4x4(previousSnapshotWorld) : W;
    XMMATRIX WIT = XMMatrixTranspose(XMMatrixInverse(nullptr, W));
    XMStoreFloat4x4(&objConstants.gWorld, XMMatrixTranspose(W));
    XMStoreFloat4x4(&objConstants.gWorldInvTranspose, WIT);
    XMStoreFloat4x4(&objConstants.gPrevWorld, XMMatrixTranspose(prevW));

    memcpy(m_ObjectConstantBufferDataBegin[frameIndex] + (objectIndex * m_ObjectConstantBufferSize),
        &objConstants, sizeof(ObjectConstants));

    D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = m_ObjectConstantBuffers[frameIndex]->GetGPUVirtualAddress()
        + (objectIndex * m_ObjectConstantBufferSize);
    commandList->SetGraphicsRootConstantBufferView(0, objCBAddress);

    RenderMaterialConstants matConstants = {};
    matConstants.gDiffuseAlbedo = material->albedo;

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

    if (objectIndex == 0) {
        D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = m_PassConstantBuffers[frameIndex]->GetGPUVirtualAddress();
        commandList->SetGraphicsRootConstantBufferView(2, passCBAddress);
    }

    D3D12_GPU_VIRTUAL_ADDRESS skinCBAddress =
        m_SkinningConstantBuffers[frameIndex]->GetGPUVirtualAddress();
    commandList->SetGraphicsRootConstantBufferView(3, skinCBAddress);

    ID3D12DescriptorHeap* srvHeaps[] = { m_RendererCore->GetGBufferSRVHeap() };
    commandList->SetDescriptorHeaps(1, srvHeaps);
    commandList->IASetVertexBuffers(0, 1, &mesh->vertexBufferView);
    commandList->IASetIndexBuffer(&mesh->indexBufferView);

    auto textureManager = TextureManager::Get();
    D3D12_GPU_DESCRIPTOR_HANDLE baseHandle = m_RendererCore->GetGBufferSRVHeap()->GetGPUDescriptorHandleForHeapStart();
    UINT descriptorSize = m_RendererCore->GetGBufferSRVDescriptorSize();

    auto bindAndDrawSubmesh = [&](uint32_t startIndex, uint32_t indexCount, const std::string& albedoTextureName, const std::string& normalTextureName) {
        TextureInfo* albedoTexture = textureManager->GetTexture(albedoTextureName);
        if (!albedoTexture || !albedoTexture->IsValid) {
            albedoTexture = textureManager->GetTexture("white1x1");
        }

        TextureInfo* normalTexture = textureManager->GetTexture(normalTextureName);
        if (!normalTexture || !normalTexture->IsValid) {
            normalTexture = textureManager->GetTexture("white1x1");
        }

        if (!albedoTexture || !normalTexture || !albedoTexture->IsValid || !normalTexture->IsValid) {
#ifdef _DEBUG
            OutputDebugStringA("Error: Default texture (white1x1) not loaded! Texture binding failed.\n");
#endif
            return;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE albedoHandle = baseHandle;
        albedoHandle.ptr += (8 + albedoTexture->SRVIndex) * descriptorSize;
        commandList->SetGraphicsRootDescriptorTable(4, albedoHandle);

        D3D12_GPU_DESCRIPTOR_HANDLE normalHandle = baseHandle;
        normalHandle.ptr += (8 + normalTexture->SRVIndex) * descriptorSize;
        commandList->SetGraphicsRootDescriptorTable(5, normalHandle);

        commandList->DrawIndexedInstanced(indexCount, 1, startIndex, 0, 0);
    };

    if (!mesh->submeshes.empty()) {
        for (const auto& submesh : mesh->submeshes) {
            const std::string& albedoTextureName =
                submesh.albedoTextureName.empty() ? material->albedoTextureName : submesh.albedoTextureName;
            const std::string& normalTextureName =
                submesh.normalTextureName.empty() ? material->normalTextureName : submesh.normalTextureName;

            bindAndDrawSubmesh(submesh.startIndex, submesh.indexCount, albedoTextureName, normalTextureName);
        }
    }
    else {
        bindAndDrawSubmesh(0u, static_cast<uint32_t>(mesh->indices.size()), material->albedoTextureName, material->normalTextureName);
    }
}

void RenderSystem::RenderProxyItem(const RenderProxy& proxy, UINT frameIndex, int objectIndex, const XMFLOAT4X4* snapshotWorld)
{
    auto transform = proxy.Transform;
    auto mesh = proxy.Mesh;
    auto material = proxy.Material;

    if (!transform || !mesh || !material || !mesh->vertexBuffer) {
        return;
    }

    auto commandList = m_RendererCore->GetCommandList();

    ObjectConstants objConstants = {};
    XMMATRIX W = snapshotWorld ? XMLoadFloat4x4(snapshotWorld) : transform->GetWorldMatrix();
    const XMFLOAT4X4* previousSnapshotWorld = FindPreviousSnapshotWorld(proxy.Id);
    XMMATRIX prevW = previousSnapshotWorld ? XMLoadFloat4x4(previousSnapshotWorld) : W;
    XMMATRIX WIT = XMMatrixTranspose(XMMatrixInverse(nullptr, W));
    XMStoreFloat4x4(&objConstants.gWorld, XMMatrixTranspose(W));
    XMStoreFloat4x4(&objConstants.gWorldInvTranspose, WIT);
    XMStoreFloat4x4(&objConstants.gPrevWorld, XMMatrixTranspose(prevW));

    memcpy(m_ObjectConstantBufferDataBegin[frameIndex] + (objectIndex * m_ObjectConstantBufferSize),
        &objConstants, sizeof(ObjectConstants));

    D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = m_ObjectConstantBuffers[frameIndex]->GetGPUVirtualAddress()
        + (objectIndex * m_ObjectConstantBufferSize);
    commandList->SetGraphicsRootConstantBufferView(0, objCBAddress);

    RenderMaterialConstants matConstants = {};
    matConstants.gDiffuseAlbedo = material->albedo;

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

    if (objectIndex == 0) {
        D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = m_PassConstantBuffers[frameIndex]->GetGPUVirtualAddress();
        commandList->SetGraphicsRootConstantBufferView(2, passCBAddress);
    }

    D3D12_GPU_VIRTUAL_ADDRESS skinCBAddress =
        m_SkinningConstantBuffers[frameIndex]->GetGPUVirtualAddress();
    commandList->SetGraphicsRootConstantBufferView(3, skinCBAddress);

    ID3D12DescriptorHeap* srvHeaps[] = { m_RendererCore->GetGBufferSRVHeap() };
    commandList->SetDescriptorHeaps(1, srvHeaps);
    commandList->IASetVertexBuffers(0, 1, &mesh->vertexBufferView);
    commandList->IASetIndexBuffer(&mesh->indexBufferView);

    auto textureManager = TextureManager::Get();
    D3D12_GPU_DESCRIPTOR_HANDLE baseHandle = m_RendererCore->GetGBufferSRVHeap()->GetGPUDescriptorHandleForHeapStart();
    UINT descriptorSize = m_RendererCore->GetGBufferSRVDescriptorSize();

    auto bindAndDrawSubmesh = [&](uint32_t startIndex, uint32_t indexCount, const std::string& albedoTextureName, const std::string& normalTextureName) {
        TextureInfo* albedoTexture = textureManager->GetTexture(albedoTextureName);
        if (!albedoTexture || !albedoTexture->IsValid) {
            albedoTexture = textureManager->GetTexture("white1x1");
        }

        TextureInfo* normalTexture = textureManager->GetTexture(normalTextureName);
        if (!normalTexture || !normalTexture->IsValid) {
            normalTexture = textureManager->GetTexture("white1x1");
        }

        if (!albedoTexture || !normalTexture || !albedoTexture->IsValid || !normalTexture->IsValid) {
#ifdef _DEBUG
            OutputDebugStringA("Error: Default texture (white1x1) not loaded! Texture binding failed.\n");
#endif
            return;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE albedoHandle = baseHandle;
        albedoHandle.ptr += (8 + albedoTexture->SRVIndex) * descriptorSize;
        commandList->SetGraphicsRootDescriptorTable(4, albedoHandle);

        D3D12_GPU_DESCRIPTOR_HANDLE normalHandle = baseHandle;
        normalHandle.ptr += (8 + normalTexture->SRVIndex) * descriptorSize;
        commandList->SetGraphicsRootDescriptorTable(5, normalHandle);

        commandList->DrawIndexedInstanced(indexCount, 1, startIndex, 0, 0);
    };

    if (!mesh->submeshes.empty()) {
        for (const auto& submesh : mesh->submeshes) {
            const std::string& albedoTextureName =
                submesh.albedoTextureName.empty() ? material->albedoTextureName : submesh.albedoTextureName;
            const std::string& normalTextureName =
                submesh.normalTextureName.empty() ? material->normalTextureName : submesh.normalTextureName;

            bindAndDrawSubmesh(submesh.startIndex, submesh.indexCount, albedoTextureName, normalTextureName);
        }
    }
    else {
        bindAndDrawSubmesh(0u, static_cast<uint32_t>(mesh->indices.size()), material->albedoTextureName, material->normalTextureName);
    }
}

void RenderSystem::RenderVelocityProxyItem(const RenderProxy& proxy, UINT frameIndex, int objectIndex, const XMFLOAT4X4* snapshotWorld)
{
    auto transform = proxy.Transform;
    auto mesh = proxy.Mesh;

    if (!transform || !mesh || !mesh->vertexBuffer) {
        return;
    }

    auto commandList = m_RendererCore->GetCommandList();

    ObjectConstants objConstants = {};
    XMMATRIX W = snapshotWorld ? XMLoadFloat4x4(snapshotWorld) : transform->GetWorldMatrix();
    const XMFLOAT4X4* previousSnapshotWorld = FindPreviousSnapshotWorld(proxy.Id);
    XMMATRIX prevW = previousSnapshotWorld ? XMLoadFloat4x4(previousSnapshotWorld) : W;
    XMMATRIX WIT = XMMatrixTranspose(XMMatrixInverse(nullptr, W));
    XMStoreFloat4x4(&objConstants.gWorld, XMMatrixTranspose(W));
    XMStoreFloat4x4(&objConstants.gWorldInvTranspose, WIT);
    XMStoreFloat4x4(&objConstants.gPrevWorld, XMMatrixTranspose(prevW));

    memcpy(m_ObjectConstantBufferDataBegin[frameIndex] + (objectIndex * m_ObjectConstantBufferSize),
        &objConstants, sizeof(ObjectConstants));

    D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = m_ObjectConstantBuffers[frameIndex]->GetGPUVirtualAddress()
        + (objectIndex * m_ObjectConstantBufferSize);
    commandList->SetGraphicsRootConstantBufferView(0, objCBAddress);

    D3D12_GPU_VIRTUAL_ADDRESS skinCBAddress =
        m_SkinningConstantBuffers[frameIndex]->GetGPUVirtualAddress();
    commandList->SetGraphicsRootConstantBufferView(2, skinCBAddress);

    commandList->IASetVertexBuffers(0, 1, &mesh->vertexBufferView);
    commandList->IASetIndexBuffer(&mesh->indexBufferView);

    if (!mesh->submeshes.empty()) {
        for (const auto& submesh : mesh->submeshes) {
            commandList->DrawIndexedInstanced(submesh.indexCount, 1, submesh.startIndex, 0, 0);
        }
    }
    else {
        commandList->DrawIndexedInstanced(static_cast<UINT>(mesh->indices.size()), 1, 0, 0, 0);
    }
}

void RenderSystem::RefreshRenderSnapshot()
{
    m_PreviousSnapshotWorldByEntity = m_SnapshotWorldByEntity;
    if (m_HasCurrentViewProjMatrix) {
        m_PreviousViewMatrix = m_CurrentViewMatrix;
        m_PreviousProjMatrix = m_CurrentProjMatrix;
        m_PreviousViewProjMatrix = m_CurrentViewProjMatrix;
        m_HasPreviousViewProjMatrix = true;
    }

    // 매 프레임 빈 렌더 스냅샷에서 시작한다. physics가 아직 publish하지 않았으면
    // draw 코드는 실시간 transform pointer를 fallback으로 사용한다.
    m_CurrentRenderSnapshot.Clear();
    m_SnapshotWorldByEntity.clear();

    if (!m_Engine) {
        return;
    }

    SnapshotBuffer* snapshotBuffer = m_Engine->GetPhysicsSnapshotBuffer();
    if (!snapshotBuffer) {
        return;
    }

    const float alpha = m_Engine->GetPhysicsInterpolationAlpha();
    // AcquirePair는 SnapshotBuffer lock 안에서 physics history를 복사한다.
    // CameraLogicState도 함께 복사해 draw 패스가 프레임 로컬 값을 사용하게 한다.
    m_CurrentRenderSnapshot = m_RenderSnapshotBuilder.Build(
        snapshotBuffer->AcquirePair(),
        alpha,
        m_Engine->GetCameraLogicState());

    for (const RenderDrawItemSnapshot& item : m_CurrentRenderSnapshot.GetDrawItems()) {
        // object constant를 바인딩할 때 모든 draw 패스가 쓰는 빠른 조회 테이블.
        m_SnapshotWorldByEntity[item.Id] = item.World;
    }
}

const XMFLOAT4X4* RenderSystem::FindSnapshotWorld(uint32_t entityId) const
{
    auto it = m_SnapshotWorldByEntity.find(entityId);
    if (it == m_SnapshotWorldByEntity.end()) {
        return nullptr;
    }

    return &it->second;
}

const XMFLOAT4X4* RenderSystem::FindPreviousSnapshotWorld(uint32_t entityId) const
{
    auto it = m_PreviousSnapshotWorldByEntity.find(entityId);
    if (it == m_PreviousSnapshotWorldByEntity.end()) {
        return nullptr;
    }

    return &it->second;
}

void RenderSystem::ProcessRenderCommands()
{
    // 렌더 스레드에서 프레임당 한 번만 큐를 비운다.
    // 생산자 스레드는 현재 프레임이 복사된 명령 목록을 처리하는 동안 계속 push할 수 있다.
    for (RenderCommand& command : m_RenderCommandQueue.Drain()) {
        switch (command.Type) {
        case RenderCommandType::RegisterEntity: {
            EnsureMeshResources(command.Mesh);

            // RenderProxy는 엔티티를 렌더 스레드 소유 데이터로 표현한 사본이다.
            auto existing = std::find_if(
                m_RenderProxies.begin(),
                m_RenderProxies.end(),
                [&](const RenderProxy& proxy) { return proxy.Id == command.Id; });

            RenderProxy proxy;
            proxy.Id = command.Id;
            proxy.Transform = command.Transform;
            proxy.Mesh = command.Mesh;
            proxy.Material = command.Material;
            proxy.Active = command.Active;

            if (existing != m_RenderProxies.end()) {
                *existing = proxy;
            }
            else {
                m_RenderProxies.push_back(proxy);
            }
            break;
        }
        case RenderCommandType::UnregisterEntity:
            m_RenderProxies.erase(
                std::remove_if(
                    m_RenderProxies.begin(),
                    m_RenderProxies.end(),
                    [&](const RenderProxy& proxy) { return proxy.Id == command.Id; }),
                m_RenderProxies.end());
            break;
        case RenderCommandType::UpdateMesh:
        case RenderCommandType::UpdateMaterial:
        case RenderCommandType::SetActiveCamera:
        case RenderCommandType::Shutdown:
            break;
        }
    }
}

void RenderSystem::EnsureMeshResources(MeshComponent* mesh)
{
    if (!mesh || !mesh->isLoaded || mesh->vertexBuffer) {
        return;
    }

    auto device = m_RendererCore->GetDevice();

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = sizeof(Vertex) * mesh->vertices.size();
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mesh->vertexBuffer));

    UINT8* pVertexDataBegin;
    mesh->vertexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pVertexDataBegin));
    memcpy(pVertexDataBegin, mesh->vertices.data(), sizeof(Vertex) * mesh->vertices.size());
    mesh->vertexBuffer->Unmap(0, nullptr);

    mesh->vertexBufferView.BufferLocation = mesh->vertexBuffer->GetGPUVirtualAddress();
    mesh->vertexBufferView.StrideInBytes = sizeof(Vertex);
    mesh->vertexBufferView.SizeInBytes = static_cast<UINT>(sizeof(Vertex) * mesh->vertices.size());

    bufferDesc.Width = sizeof(uint32_t) * mesh->indices.size();
    device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&mesh->indexBuffer));

    UINT8* pIndexDataBegin;
    mesh->indexBuffer->Map(0, nullptr, reinterpret_cast<void**>(&pIndexDataBegin));
    memcpy(pIndexDataBegin, mesh->indices.data(), sizeof(uint32_t) * mesh->indices.size());
    mesh->indexBuffer->Unmap(0, nullptr);

    mesh->indexBufferView.BufferLocation = mesh->indexBuffer->GetGPUVirtualAddress();
    mesh->indexBufferView.Format = DXGI_FORMAT_R32_UINT;
    mesh->indexBufferView.SizeInBytes = static_cast<UINT>(sizeof(uint32_t) * mesh->indices.size());
}

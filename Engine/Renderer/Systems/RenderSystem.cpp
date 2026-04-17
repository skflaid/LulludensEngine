#include "App/GameEngine.h"
#include "Inference/WinMLStyleTransferSystem.h"
#include "RenderSystem.h"
#include "../Components/TransformComponent.h"
#include "../Components/MeshComponent.h"
#include "../Components/MaterialComponent.h"
#include "../Common/d3dUtil.h"
#include "Renderer/Components/CameraComponent.h"
#include "Renderer/Components/SkyComponent.h"
#include <d3dcompiler.h>
#include "Renderer/Components/SkeletonComponent.h"
#include "Core/TextureManager.h"
#include "EnvironmentManager.h"
#include "SkyRenderer.h"

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
    // 移대찓??酉? ?됰젹 ?ㅼ젙
    XMVECTOR eye = XMVectorSet(0.0f, 3.0f, -8.0f, 0.0f);  // 移대찓???꾩튂
    XMVECTOR at = XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f);    // 諛붾씪蹂대뒗 吏??
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);    // ?곹뼢 踰≫꽣
    XMStoreFloat4x4(&m_ViewMatrix, XMMatrixLookAtLH(eye, at, up));

    // ?먭렐 ?ъ쁺 ?됰젹 ?ㅼ젙
    float fov = XM_PIDIV4; // 45??
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

    // CapstoneDesign에서 export한 ONNX 모델을 Lighting 이후에 연결한다.
    WinMLStyleTransferSystem::Config styleConfig = {};
    styleConfig.modelPath = L"C:\\LocalRepository\\CapstoneDesign\\Learning\\net4\\net4.onnx";
    styleConfig.inputWidth = 640;
    styleConfig.inputHeight = 360;
    m_WinMLStyleTransferSystem = std::make_unique<WinMLStyleTransferSystem>(m_RendererCore.get(), styleConfig);
    m_WinMLStyleTransferSystem->Initialize();
    
    CreateGBufferPipelineState();
    CreateShadowPipelineState();
    CreateLightingPipelineState();
    CreateBackgroundResolvePipelineState();
    CreateSSGIPipelineState();
    CreateSSGIDenoisePipelineState();
}

void RenderSystem::InitializeTextures() {
    // TextureManager 珥덇린??
    auto textureManager = TextureManager::Get();
    textureManager->SetSRVHeap(m_RendererCore->GetGBufferSRVHeap(), m_RendererCore->GetGBufferSRVDescriptorSize());
    
    // 紐⑤뱺 DDS ?띿뒪泥?濡쒕뱶
    auto device = m_RendererCore->GetDevice();
    auto commandList = m_RendererCore->GetCommandList();
    
    // Command list ?닿린
    commandList->Reset(m_RendererCore->GetCommandAllocator(0), nullptr);
    
    if (!textureManager->LoadAllDDSFromDirectory(device, commandList)) {
        // ?띿뒪泥?濡쒕뱶 ?ㅽ뙣 - ?먮윭 異쒕젰 (?붾쾭洹?鍮뚮뱶?먯꽌留?
        #ifdef _DEBUG
        OutputDebugStringA("Warning: Failed to load DDS textures from directory\n");
        #endif
    }
    
    // Command list ?ㅽ뻾 諛??湲?
    commandList->Close();
    ID3D12CommandQueue* commandQueue = m_RendererCore->GetCommandQueue();
    ID3D12CommandList* cmdLists[] = { commandList };
    commandQueue->ExecuteCommandLists(1, cmdLists);
    
    // GPU ?숆린??(?띿뒪泥??낅줈???꾨즺 ?湲?
    // ?띿뒪泥??낅줈?쒓? ?꾨즺???뚭퉴吏 ?湲고븯??CommandAllocator 由ъ뀑 臾몄젣 諛⑹?
    m_RendererCore->FlushCommandQueue();
}

void RenderSystem::CreateConstantBuffer() {
    auto device = m_RendererCore->GetDevice();

    // ?곸닔 踰꾪띁??256諛붿씠??諛곗닔濡??뺣젹?섏뼱????
    m_ObjectConstantBufferSize = (sizeof(ObjectConstants) + 255) & ~255;
    m_MaterialConstantBufferSize = (sizeof(RenderMaterialConstants) + 255) & ~255;
    m_PassConstantBufferSize = (sizeof(PassConstants) + 255) & ~255;
    m_SkinningConstantBufferSize = (sizeof(SkinningConstants) + 255) & ~255;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;

    // Object constant buffer (b0) - 100媛??ㅻ툕?앺듃源뚯? 吏??
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

        // Material constant buffers (b1) - 100媛?癒명떚由ъ뼹源뚯? 吏??
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

        // Pass constant buffer (b2) - ?꾨젅?꾨떦 1媛?
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

    // ?띿뒪泥?SRV ?뚯씠釉?(?뚮퉬?? ?몃쭚留? - 媛곴컖 蹂꾨룄??descriptor table濡?遺꾨━
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

    // Root Signature (CBV b0, b1, b2, b3, SRV Table for Albedo, SRV Table for Normal)
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

    // b3 : cbSkinning (蹂??붾젅??
    rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    rootParameters[3].Descriptor.ShaderRegister = 3; // b3
    rootParameters[3].Descriptor.RegisterSpace = 0;
    rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
    
    // ?뚮퉬???띿뒪泥?SRV ?뚯씠釉?(t0)
    rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[4].DescriptorTable.pDescriptorRanges = srvTableAlbedo;
    rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    
    // ?몃쭚留??띿뒪泥?SRV ?뚯씠釉?(t1)
    rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParameters[5].DescriptorTable.NumDescriptorRanges = 1;
    rootParameters[5].DescriptorTable.pDescriptorRanges = srvTableNormal;
    rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // ?섑뵆???ㅼ젙
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

void RenderSystem::CreateBackgroundResolvePipelineState()
{
    auto device = m_RendererCore->GetDevice();

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
    // s0 : 湲곗〈 GBuffer/SSGI???ъ씤???섑뵆??
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    samplers[0].ShaderRegister = 0;
    samplers[0].RegisterSpace = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1 : ShadowMap??鍮꾧탳 ?섑뵆??
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
    rootDesc.NumStaticSamplers = 2;              // ??2媛?
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

    // Shadow depth ?띿뒪泥?由ъ냼???앹꽦 (R24G8 typeless)
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
        D3D12_RESOURCE_STATE_GENERIC_READ,    // ?섏쨷??DEPTH_WRITE ??GENERIC_READ ?꾪솚
        &optClear,
        IID_PPV_ARGS(&m_ShadowMap)));

    // DSV heap 1媛쒖쭨由?
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

    // ??SRV??G-Buffer SRV Heap ?덉뿉??RendererCore媛 留뚮뱾?댁쨾????
    // DXGI_FORMAT_R24_UNORM_X8_TYPELESS ?щ㎎?쇰줈 SRV ?앹꽦?댁꽌 Lighting.hlsl t5??諛붿씤??

    // ShadowMap SRV: DSV??D24_UNORM_S8_UINT, SRV??R24_UNORM_X8_TYPELESS 濡?留뚮뱾?댁빞 ??
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    // GBuffer SRV heap??t5 ?먮━???앹꽦 (Position=t0, Normal=t1, Albedo=t2, Material=t3, SSGI=t4, Shadow=t5)
    auto shadowSrvHandle = m_RendererCore->GetGBufferSRVHandle(5);
    device->CreateShaderResourceView(m_ShadowMap.Get(), &srvDesc, shadowSrvHandle);
}

void RenderSystem::CreateShadowPipelineState() {
    auto device = m_RendererCore->GetDevice();

    const std::wstring shaderPath = L"Renderer/Shaders/ShadowMap.hlsl";
    ComPtr<ID3DBlob> vs, ps;
    vs = d3dUtil::CompileShader(shaderPath, nullptr, "VS", "vs_5_0");
    ps = d3dUtil::CompileShader(shaderPath, nullptr, "PS", "ps_5_0");

    // GBuffer? ?숈씪???명뭼 ?덉씠?꾩썐
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
    rastDesc.DepthBias = 100000;              // ??depth bias
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
    pso.pRootSignature = m_GBufferRootSignature.Get();         // ??GBuffer rootSig ?ъ궗??
    pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
    pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
    pso.RasterizerState = rastDesc;
    pso.BlendState = blendDesc;
    pso.DepthStencilState = dsDesc;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    pso.NumRenderTargets = 0;                                // ??而щ윭 RT ?놁쓬
    pso.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
    pso.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;           // CreateShadowResources? 留욎떠????
    pso.SampleDesc.Count = 1;

    ThrowIfFailed(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&m_ShadowPipelineState)));
}


void RenderSystem::Update(float deltaTime) {
    m_TotalTime += deltaTime;
    m_DeltaTime = deltaTime;

    // 1) ?ㅼ펷?덊넠 媛吏??뷀떚???섎굹 李얘린
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

    // 2) FinalBoneTransforms ??SkinningConstants 蹂듭궗
    SkinningConstants skin = {};
    const uint32_t boneCount = (std::min)(
        static_cast<uint32_t>(skeleton->FinalBoneTransforms.size()),
        static_cast<uint32_t>(MAX_BONES)
        );

    for (size_t i = 0; i < boneCount; ++i) {
        XMMATRIX M = XMLoadFloat4x4(&skeleton->FinalBoneTransforms[i]);
        XMStoreFloat4x4(&skin.BoneTransforms[i], XMMatrixTranspose(M));
    }
    // ?⑤뒗 ?щ’? Identity濡?
    for (size_t i = boneCount; i < MAX_BONES; ++i) {
        DirectX::XMStoreFloat4x4(
            &skin.BoneTransforms[i],
            DirectX::XMMatrixIdentity()
        );
    }

    // 3) 紐⑤뱺 ?꾨젅?꾩쓽 cbSkinning 踰꾪띁???⑥＜湲?(FrameCount 媛?
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

    if (m_RendererCore) {
        m_RendererCore->Shutdown();
    }
}

void RenderSystem::RegisterEntity(Entity* entity) {
    if (entity->HasComponent<MeshComponent>() && entity->HasComponent<TransformComponent>()) {
        m_RenderableEntities.push_back(entity);

        // GPU??硫붿떆 ?낅줈??
        auto* meshComp = entity->GetComponent<MeshComponent>();
        if (meshComp && meshComp->isLoaded && !meshComp->vertexBuffer) {
            auto device = m_RendererCore->GetDevice();

            // Vertex Buffer ?앹꽦
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

            // Index Buffer ?앹꽦
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

void RenderSystem::SetActiveSky(Entity* skyEntity)
{
    if (m_EnvironmentManager) {
        m_EnvironmentManager->SetActiveSky(skyEntity);
    }
}

void RenderSystem::Render() {
    m_RendererCore->BeginFrame();

    UINT frameIndex = 0;
    UpdatePassConstants(frameIndex);

    RenderShadowPass(frameIndex);

    // G-Buffer Pass
    RenderGBufferPass(frameIndex);
    RenderBackgroundResolvePass(frameIndex);

    // SSGI Pass 
    RenderSSGIPass(frameIndex);

    // SSGI Denoise Pass 
    RenderSSGIDenoisePass(frameIndex);


    CopySSGIToPrevious(frameIndex);

    // Lighting Pass
    RenderLightingPass(frameIndex);
    RenderSkyPass(frameIndex);

    // 추론이 가능하면 스타일 결과를, 아니면 원본 lighting 결과를 바로 출력한다.
    if (m_IsStyleTransferEnabled &&
        m_WinMLStyleTransferSystem &&
        m_WinMLStyleTransferSystem->Execute()) {
        CopyFrameToBackBuffer(m_WinMLStyleTransferSystem->GetOutputTexture());
    }
    else {
        CopyFrameToBackBuffer(m_RendererCore->GetLightingBuffer());
    }

    m_RendererCore->EndFrame();
    m_RendererCore->Present();
}

void RenderSystem::RenderShadowPass(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();

    // Shadow viewport & scissor
    commandList->RSSetViewports(1, &m_ShadowViewport);
    commandList->RSSetScissorRects(1, &m_ShadowScissorRect);

    // Shadow map??DEPTH_WRITE ?곹깭濡?
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_ShadowMap.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_GENERIC_READ;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // 源딆씠留??대━??
    commandList->ClearDepthStencilView(m_ShadowDsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    commandList->OMSetRenderTargets(0, nullptr, FALSE, &m_ShadowDsv);

    commandList->SetPipelineState(m_ShadowPipelineState.Get());
    commandList->SetGraphicsRootSignature(m_GBufferRootSignature.Get());
    commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    int objectIndex = 0;
    for (Entity* entity : m_RenderableEntities) {
        if (entity && entity->IsActive()) {
            // Object/Material/Pass CBV ?뗭뾽 + draw
            RenderEntity(entity, frameIndex, objectIndex);
            ++objectIndex;
        }
    }

    // ?ㅼ떆 ?섑뵆留곸슜 ?곹깭濡?
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
    commandList->ResourceBarrier(1, &barrier);
}


void RenderSystem::RenderGBufferPass(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();
    auto device = m_RendererCore->GetDevice();

    // ShadowPass?먯꽌 諛붾?酉고룷???쒖?瑜?硫붿씤 ?붾㈃ 湲곗??쇰줈 蹂듭썝
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
    // barrier 諛곗뿴????긽 珥덇린??(?섏쨷???ㅼ떆 ?ъ슜?섍린 ?꾪빐)
    // Position/Normal/Albedo/Material/Depth 5개 MRT를 모두 render target 상태로 맞춘다.
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
    
    // 泥??꾨젅?꾩뿉?쒕뒗 G-Buffer媛 ?대? RENDER_TARGET ?곹깭濡??쒖옉
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

    // Set G-Buffer render targets
    // 마지막 슬롯은 StyleTransfer 입력용 depth MRT다.
    D3D12_CPU_DESCRIPTOR_HANDLE gbufferRTVs[5] = {
        m_RendererCore->GetGBufferRTVHandle(0),
        m_RendererCore->GetGBufferRTVHandle(1),
        m_RendererCore->GetGBufferRTVHandle(2),
        m_RendererCore->GetGBufferRTVHandle(3),
        m_RendererCore->GetGBufferRTVHandle(4)
    };
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_RendererCore->GetDSVHeap()->GetCPUDescriptorHandleForHeapStart();

    // Clear G-Buffer
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

    // Set pipeline state
    commandList->SetPipelineState(m_GBufferPipelineState.Get());
    commandList->SetGraphicsRootSignature(m_GBufferRootSignature.Get());
    commandList->OMSetStencilRef(1);
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
    // barrier 諛곗뿴 ?ъ큹湲고솕 (?덉쟾?섍쾶)
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
    // Lighting은 백버퍼가 아니라 별도 LightingBuffer에 먼저 기록한다.
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

    // 최종 출력은 단순 CopyResource로 백버퍼에 써서 후속 present로 넘긴다.
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

void RenderSystem::ToggleStyleTransfer() {
    m_IsStyleTransferEnabled = !m_IsStyleTransferEnabled;
}

void RenderSystem::RenderSSGIPass(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();
    auto device = m_RendererCore->GetDevice();

    // Transition SSGI buffer to unordered access state
    // 泥??꾨젅?꾩뿉?쒕뒗 ?대? UNORDERED_ACCESS ?곹깭?대?濡?barrier瑜?嫄대꼫?
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

    // Set descriptor heap (G-Buffer SRV Heap??紐⑤뱺 descriptor媛 ?ы븿?섏뼱 ?덉쓬)
    ID3D12DescriptorHeap* heaps[] = { m_RendererCore->GetGBufferSRVHeap() };
    commandList->SetDescriptorHeaps(1, heaps);

    // Set G-Buffer SRVs
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = m_RendererCore->GetGBufferSRVHeap()->GetGPUDescriptorHandleForHeapStart();
    commandList->SetComputeRootDescriptorTable(1, srvHandle);

    // Set SSGI UAV (G-Buffer SRV Heap?먯꽌 媛?몄샂)
    D3D12_GPU_DESCRIPTOR_HANDLE uavHandle = m_RendererCore->GetSSGIUAVHandleFromGBufferHeap();
    commandList->SetComputeRootDescriptorTable(2, uavHandle);

    // Dispatch compute shader (8x8 thread groups)
    uint32_t width = m_RendererCore->GetWidth();
    uint32_t height = m_RendererCore->GetHeight();
    uint32_t dispatchX = (width + 7) / 8;
    uint32_t dispatchY = (height + 7) / 8;
    commandList->Dispatch(dispatchX, dispatchY, 1);

    // SSGI 踰꾪띁??UNORDERED_ACCESS ?곹깭濡??좎?
    // Denoise ?⑥뒪?먯꽌 SRV濡??쎄린 ?꾪빐 ?곹깭 ?꾪솚???덉젙
    
    // 泥??꾨젅???뚮옒洹??댁젣
    m_IsFirstSSGIFrame = false;
}

void RenderSystem::RenderSSGIDenoisePass(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();
    auto device = m_RendererCore->GetDevice();

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_RendererCore->GetSSGIBuffer();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;  // SSGI ?⑥뒪?먯꽌 UAV濡?異쒕젰???곹깭
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;  // Denoise?먯꽌 SRV濡??쎄린
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // Set pipeline state
    commandList->SetPipelineState(m_SSGIDenoisePipelineState.Get());
    commandList->SetComputeRootSignature(m_SSGIDenoiseRootSignature.Get());

    // Set pass constant buffer
    D3D12_GPU_VIRTUAL_ADDRESS passCBAddress = m_PassConstantBuffers[frameIndex]->GetGPUVirtualAddress();
    commandList->SetComputeRootConstantBufferView(0, passCBAddress);

    // Set descriptor heap (G-Buffer SRV Heap??紐⑤뱺 descriptor媛 ?ы븿?섏뼱 ?덉쓬)
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
    
    // Set SSGI Input SRV (G-Buffer SRV Heap??4踰덉㎏ ?щ’)
    D3D12_GPU_DESCRIPTOR_HANDLE ssgiSrvHandle = m_RendererCore->GetSSGISRVHandleFromGBufferHeap();
    commandList->SetComputeRootDescriptorTable(3, ssgiSrvHandle);
    
    // Set SSGI Previous SRV (G-Buffer SRV Heap??7踰덉㎏ ?щ’)
    D3D12_GPU_DESCRIPTOR_HANDLE ssgiPreviousSrvHandle = m_RendererCore->GetSSGIPreviousSRVHandleFromGBufferHeap();
    commandList->SetComputeRootDescriptorTable(4, ssgiPreviousSrvHandle);
    
    // Set SSGI Output UAV (G-Buffer SRV Heap??6踰덉㎏ ?щ’)
    D3D12_GPU_DESCRIPTOR_HANDLE uavHandle = m_RendererCore->GetSSGIUAVHandleFromGBufferHeap();
    commandList->SetComputeRootDescriptorTable(5, uavHandle);

    // SSGI 踰꾪띁瑜?UAV濡??곌린 ?꾪빐 ?곹깭 ?꾪솚
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;  // Denoise 異쒕젰??
    commandList->ResourceBarrier(1, &barrier);

    // Dispatch compute shader (8x8 thread groups)
    uint32_t width = m_RendererCore->GetWidth();
    uint32_t height = m_RendererCore->GetHeight();
    uint32_t dispatchX = (width + 7) / 8;
    uint32_t dispatchY = (height + 7) / 8;
    commandList->Dispatch(dispatchX, dispatchY, 1);

    // SSGI 踰꾪띁瑜?pixel shader resource ?곹깭濡??꾪솚 (Lighting ?⑥뒪?먯꽌 ?ъ슜)
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
}

void RenderSystem::CopySSGIToPrevious(UINT frameIndex) {
    auto commandList = m_RendererCore->GetCommandList();
    
    // ?꾩옱 SSGI 踰꾪띁瑜?COPY_SOURCE ?곹깭濡??꾪솚
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = m_RendererCore->GetSSGIBuffer();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    
    // ?댁쟾 ?꾨젅??SSGI 踰꾪띁瑜?COPY_DEST ?곹깭濡??꾪솚
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = m_RendererCore->GetSSGIPreviousBuffer();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    
    commandList->ResourceBarrier(2, barriers);
    
    // ?띿뒪泥?蹂듭궗
    commandList->CopyResource(
        m_RendererCore->GetSSGIPreviousBuffer(),
        m_RendererCore->GetSSGIBuffer()
    );
    
    // ?곹깭瑜??먮옒?濡?蹂듭썝
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    
    commandList->ResourceBarrier(2, barriers);
}

void RenderSystem::UpdatePassConstants(UINT frameIndex) {
    PassConstants passConstants = {};
    
    // GameEngine?먯꽌 硫붿씤 移대찓?쇰? 媛?몄샃?덈떎.
    Entity* mainCamera = m_Engine->GetMainCamera();
    if (!mainCamera) {
        // 移대찓?쇨? ?놁쑝硫??뚮뜑留?以묐떒 (?먮뒗 湲곕낯 ?됰젹 ?ъ슜)
        return;
    }    
    
    auto cameraComp = mainCamera->GetComponent<CameraComponent>();
    if (!cameraComp) return;

    // RenderSystem??硫ㅻ쾭 蹂????? CameraComponent???됰젹??吏곸젒 媛?몄샃?덈떎.
    XMMATRIX V = XMLoadFloat4x4(&cameraComp->ViewMatrix);
    XMMATRIX P = XMLoadFloat4x4(&cameraComp->ProjMatrix);
    XMMATRIX VP = XMMatrixMultiply(V, P);
    
    XMStoreFloat4x4(&passConstants.gView, XMMatrixTranspose(V));
    XMStoreFloat4x4(&passConstants.gInvView, XMMatrixTranspose(XMMatrixInverse(nullptr, V)));
    XMStoreFloat4x4(&passConstants.gProj, XMMatrixTranspose(P));
    XMStoreFloat4x4(&passConstants.gInvProj, XMMatrixTranspose(XMMatrixInverse(nullptr, P)));
    XMStoreFloat4x4(&passConstants.gViewProj, XMMatrixTranspose(VP));
    XMStoreFloat4x4(&passConstants.gInvViewProj, XMMatrixTranspose(XMMatrixInverse(nullptr, VP)));
    
    // === ?ш린遺???쇱씠??湲곗? Shadow ?됰젹 怨꾩궛 ===
    // 1) 諛⑺뼢愿?0踰덉쓽 諛⑺뼢 ?ъ슜
    XMVECTOR lightDir = XMVector3Normalize(
        XMLoadFloat3(&passConstants.gLights[0].Direction)
    );

    // ?뱀떆 0踰≫꽣硫?湲곕낯 諛⑺뼢 ?ъ슜
    if (XMVector3Less(XMVector3LengthSq(lightDir), XMVectorReplicate(0.001f)))
    {
        lightDir = XMVectorSet(0.577f, -0.577f, 0.577f, 0.0f);
    }

    // 2) ??꾩슦媛 鍮꾩텧 ?寃??꾩튂 (?쇰떒 ?붾뱶 ?먯젏 洹쇱쿂濡?
    XMVECTOR targetPos = XMVectorZero();

    // ?쇱씠???꾩튂 = ?寃?- dir * distance
    const float lightDist = 50.0f; // ??洹쒕え 蹂닿퀬 ?곷떦??議곗젅
    XMVECTOR lightPos = XMVectorMultiplyAdd(
        XMVectorReplicate(-lightDist),
        lightDir,
        targetPos
    );

    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, up);

    // 3) 吏곴탳 ?꾨줈?앹뀡 (??꾩슦 踰붿쐞)
    float l = -50.0f, r = 50.0f;
    float b = -50.0f, t = 50.0f;
    float n = 1.0f, f = 150.0f;
    XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

    XMMATRIX lightViewProj = lightView * lightProj;

    XMStoreFloat4x4(&passConstants.gShadowView, XMMatrixTranspose(lightView));
    XMStoreFloat4x4(&passConstants.gShadowProj, XMMatrixTranspose(lightProj));
    XMStoreFloat4x4(&passConstants.gShadowViewProj, XMMatrixTranspose(lightViewProj));

    // 4) NDC(-1~1) ???띿뒪泥?0~1) 蹂???됰젹
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
    passConstants.gDeltaTime = m_DeltaTime;
    passConstants.gAmbientLight = m_AmbientLight;
    passConstants.gRenderMode = static_cast<int>(m_RenderMode);
    passConstants.cbPerObjectPad3 = 0.0f;
    passConstants.cbPerObjectPad4 = XMFLOAT2(0.0f, 0.0f);
    
    // 湲곕낯 諛⑺뼢愿?4媛??ㅼ젙 (?곗씠?붽? NUM_DIR_LIGHTS=4??湲곕???
    // 泥?踰덉㎏ ?쇱씠?? ?꾩뿉???꾨옒濡?
    passConstants.gLights[0].Strength = XMFLOAT3(0.9f, 0.9f, 0.9f);
    passConstants.gLights[0].Direction = XMFLOAT3(0.0f, -1.0f, 0.0f);
    passConstants.gLights[0].FalloffStart = 1.0f;
    passConstants.gLights[0].FalloffEnd = 1000.0f;
    passConstants.gLights[0].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
    passConstants.gLights[0].SpotPower = 1.0f;
    
    // ??踰덉㎏ ?쇱씠?? ?쎄컙??蹂댁“愿?
    passConstants.gLights[1].Strength = XMFLOAT3(0.3f, 0.3f, 0.3f);
    passConstants.gLights[1].Direction = XMFLOAT3(-0.5f, -0.5f, -0.5f);
    passConstants.gLights[1].FalloffStart = 1.0f;
    passConstants.gLights[1].FalloffEnd = 10.0f;
    passConstants.gLights[1].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
    passConstants.gLights[1].SpotPower = 64.0f;
    
    // ??踰덉㎏ ?쇱씠?? ?쎄컙??蹂댁“愿?
    passConstants.gLights[2].Strength = XMFLOAT3(0.2f, 0.2f, 0.2f);
    passConstants.gLights[2].Direction = XMFLOAT3(0.5f, -0.5f, 0.5f);
    passConstants.gLights[2].FalloffStart = 1.0f;
    passConstants.gLights[2].FalloffEnd = 10.0f;
    passConstants.gLights[2].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
    passConstants.gLights[2].SpotPower = 64.0f;
    
    // ??踰덉㎏ ?쇱씠?? Z諛⑺뼢 ?ㅼ뿉???ㅻ뒗 ?쇱씠??
    passConstants.gLights[3].Strength = XMFLOAT3(0.4f, 0.4f, 0.4f);
    passConstants.gLights[3].Direction = XMFLOAT3(0.0f, 0.0f, -1.0f);
    passConstants.gLights[3].FalloffStart = 1.0f;
    passConstants.gLights[3].FalloffEnd = 1000.0f;
    passConstants.gLights[3].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
    passConstants.gLights[3].SpotPower = 1.0f;
    
    // ?섎㉧吏 ?쇱씠?몃뒗 0?쇰줈 珥덇린??
    for (int i = 4; i < 16; ++i) {
        passConstants.gLights[i].Strength = XMFLOAT3(0.0f, 0.0f, 0.0f);
        passConstants.gLights[i].Direction = XMFLOAT3(0.0f, 0.0f, 0.0f);
        passConstants.gLights[i].FalloffStart = 0.0f;
        passConstants.gLights[i].FalloffEnd = 0.0f;
        passConstants.gLights[i].Position = XMFLOAT3(0.0f, 0.0f, 0.0f);
        passConstants.gLights[i].SpotPower = 0.0f;
    }



    // 留덉?留됱뿉 memcpy 洹몃?濡??좎?
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

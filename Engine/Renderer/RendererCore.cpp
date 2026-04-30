#include "RendererCore.h"
#include <stdexcept>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

RendererCore::RendererCore()
    : m_FrameIndex(0)
    , m_RTVDescriptorSize(0)
    , m_GBufferRTVDescriptorSize(0)
    , m_GBufferSRVDescriptorSize(0)
    , m_LightingRTVDescriptorSize(0)
    , m_SSGIRTVDescriptorSize(0)
    , m_SSGISRVDescriptorSize(0)
    , m_Width(0)
    , m_Height(0)
    , m_FenceEvent(nullptr) {
    for (uint32_t i = 0; i < FrameCount; ++i) {
        m_FenceValues[i] = 0;
    }
}

RendererCore::~RendererCore() {
    Shutdown();
}

bool RendererCore::Initialize(HWND hwnd, uint32_t width, uint32_t height) {
    m_Width = width;
    m_Height = height;

    try {
        CreateDevice();
        CreateCommandQueue();
        CreateSwapChain(hwnd);
        CreateRenderTargetViews();
        CreateDepthStencilBuffer();
        CreateGBuffer();
        CreateLightingBuffer();
        CreateSSGIBuffer();
        CreateFence();

        return true;
    }
    catch (const std::exception& e) {
        // Log error
        return false;
    }
}

void RendererCore::Shutdown() {
    WaitForGPU();

    if (m_FenceEvent) {
        CloseHandle(m_FenceEvent);
        m_FenceEvent = nullptr;
    }
}

void RendererCore::CreateDevice() {
    UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
        debugController->EnableDebugLayer();
        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&m_Factory));

    D3D12CreateDevice(
        nullptr,
        D3D_FEATURE_LEVEL_11_0,
        IID_PPV_ARGS(&m_Device)
    );
}

void RendererCore::CreateCommandQueue() {
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    m_Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_CommandQueue));

    for (uint32_t i = 0; i < FrameCount; ++i) {
        m_Device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&m_FrameResources[i].CommandAllocator)
        );
        m_CommandAllocators[i] = m_FrameResources[i].CommandAllocator;
    }

    m_Device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_CommandAllocators[0].Get(),
        nullptr,
        IID_PPV_ARGS(&m_CommandList)
    );

    m_CommandList->Close();
}

void RendererCore::CreateSwapChain(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = FrameCount;
    swapChainDesc.Width = m_Width;
    swapChainDesc.Height = m_Height;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain;
    m_Factory->CreateSwapChainForHwnd(
        m_CommandQueue.Get(),
        hwnd,
        &swapChainDesc,
        nullptr,
        nullptr,
        &swapChain
    );

    swapChain.As(&m_SwapChain);
    m_SwapChain->SetMaximumFrameLatency(FrameCount);
    m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();
}

void RendererCore::CreateRenderTargetViews() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    m_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_RTVHeap));
    m_RTVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_RTVHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < FrameCount; ++i) {
        m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&m_RenderTargets[i]));
        m_Device->CreateRenderTargetView(m_RenderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.ptr += m_RTVDescriptorSize;
    }
}

void RendererCore::CreateDepthStencilBuffer() {
    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    m_Device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_DSVHeap));

    D3D12_RESOURCE_DESC depthStencilDesc = {};
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Width = m_Width;
    depthStencilDesc.Height = m_Height;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    clearValue.DepthStencil.Depth = 1.0f;
    clearValue.DepthStencil.Stencil = 0;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    m_Device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&m_DepthStencil)
    );

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.Texture2D.MipSlice = 0;

    m_Device->CreateDepthStencilView(
        m_DepthStencil.Get(),
        &dsvDesc,
        m_DSVHeap->GetCPUDescriptorHandleForHeapStart());
}

void RendererCore::CreateGBuffer() {
    // G-Buffer RTV Heap 생성 (4개 RT: Position, Normal, Albedo, Material)
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 5;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    m_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_GBufferRTVHeap));
    m_GBufferRTVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // G-Buffer SRV Heap 생성
 // 0: Position SRV
 // 1: Normal   SRV
 // 2: Albedo   SRV
 // 3: Material SRV
 // 4: SSGI     SRV
 // 5: Shadow   SRV
 // 6: SSGI     UAV
 // 7: SSGI Previous SRV (이전 프레임)
 // 8+: 텍스처 SRV들 (알비도, 노말맵 등)
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 8 + 256; // 텍스처를 위한 공간 추가 (최대 256개)
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    m_Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_GBufferSRVHeap));
    m_GBufferSRVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    
    D3D12_RESOURCE_DESC gbufferDesc = {};
    gbufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    gbufferDesc.Width = m_Width;
    gbufferDesc.Height = m_Height;
    gbufferDesc.DepthOrArraySize = 1;
    gbufferDesc.MipLevels = 1;
    gbufferDesc.SampleDesc.Count = 1;
    gbufferDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_GBufferRTVHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_GBufferSRVHeap->GetCPUDescriptorHandleForHeapStart();

    // Position Buffer (R32G32B32A32_FLOAT)
    gbufferDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 0.0f;
    m_Device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &gbufferDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearValue,
        IID_PPV_ARGS(&m_GBufferPosition)
    );
    m_Device->CreateRenderTargetView(m_GBufferPosition.Get(), nullptr, rtvHandle);
    m_Device->CreateShaderResourceView(m_GBufferPosition.Get(), nullptr, srvHandle);
    rtvHandle.ptr += m_GBufferRTVDescriptorSize;
    srvHandle.ptr += m_GBufferSRVDescriptorSize;

    // Normal Buffer (R16G16B16A16_FLOAT)
    gbufferDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    m_Device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &gbufferDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearValue,
        IID_PPV_ARGS(&m_GBufferNormal)
    );
    m_Device->CreateRenderTargetView(m_GBufferNormal.Get(), nullptr, rtvHandle);
    m_Device->CreateShaderResourceView(m_GBufferNormal.Get(), nullptr, srvHandle);
    rtvHandle.ptr += m_GBufferRTVDescriptorSize;
    srvHandle.ptr += m_GBufferSRVDescriptorSize;

    // Albedo Buffer (R8G8B8A8_UNORM)
    gbufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    m_Device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &gbufferDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearValue,
        IID_PPV_ARGS(&m_GBufferAlbedo)
    );
    m_Device->CreateRenderTargetView(m_GBufferAlbedo.Get(), nullptr, rtvHandle);
    m_Device->CreateShaderResourceView(m_GBufferAlbedo.Get(), nullptr, srvHandle);
    rtvHandle.ptr += m_GBufferRTVDescriptorSize;
    srvHandle.ptr += m_GBufferSRVDescriptorSize;

    // Material Buffer (R8G8B8A8_UNORM: Roughness, Metallic, etc.)
    gbufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    m_Device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &gbufferDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearValue,
        IID_PPV_ARGS(&m_GBufferMaterial)
    );
    m_Device->CreateRenderTargetView(m_GBufferMaterial.Get(), nullptr, rtvHandle);
    m_Device->CreateShaderResourceView(m_GBufferMaterial.Get(), nullptr, srvHandle);
    rtvHandle.ptr += m_GBufferRTVDescriptorSize;

    gbufferDesc.Format = DXGI_FORMAT_R32_FLOAT;
    clearValue.Format = DXGI_FORMAT_R32_FLOAT;
    m_Device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &gbufferDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearValue,
        IID_PPV_ARGS(&m_GBufferDepth)
    );
    m_Device->CreateRenderTargetView(m_GBufferDepth.Get(), nullptr, rtvHandle);
}

void RendererCore::CreateLightingBuffer() {
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    m_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_LightingRTVHeap));
    m_LightingRTVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_RESOURCE_DESC lightingDesc = {};
    lightingDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    lightingDesc.Width = m_Width;
    lightingDesc.Height = m_Height;
    lightingDesc.DepthOrArraySize = 1;
    lightingDesc.MipLevels = 1;
    lightingDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    lightingDesc.SampleDesc.Count = 1;
    lightingDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    m_Device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &lightingDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearValue,
        IID_PPV_ARGS(&m_LightingBuffer)
    );

    m_Device->CreateRenderTargetView(
        m_LightingBuffer.Get(),
        nullptr,
        m_LightingRTVHeap->GetCPUDescriptorHandleForHeapStart()
    );
}

void RendererCore::CreateSSGIBuffer() {
    // SSGI RTV Heap 생성
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    m_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_SSGIRTVHeap));
    m_SSGIRTVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // SSGI SRV/UAV Heap 생성 (SRV와 UAV 모두 포함)
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 2; // SRV 1개 + UAV 1개
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    m_Device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&m_SSGISRVHeap));
    m_SSGISRVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_RESOURCE_DESC ssgiDesc = {};
    ssgiDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    ssgiDesc.Width = m_Width;
    ssgiDesc.Height = m_Height;
    ssgiDesc.DepthOrArraySize = 1;
    ssgiDesc.MipLevels = 1;
    ssgiDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ssgiDesc.SampleDesc.Count = 1;
    ssgiDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 0.0f;

    m_Device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &ssgiDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        &clearValue,
        IID_PPV_ARGS(&m_SSGIBuffer)
    );

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_SSGIRTVHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = m_SSGISRVHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = srvHandle;
    uavHandle.ptr += m_SSGISRVDescriptorSize; // UAV는 SRV 다음 슬롯

    m_Device->CreateRenderTargetView(m_SSGIBuffer.Get(), nullptr, rtvHandle);
    m_Device->CreateShaderResourceView(m_SSGIBuffer.Get(), nullptr, srvHandle);
    m_Device->CreateUnorderedAccessView(m_SSGIBuffer.Get(), nullptr, nullptr, uavHandle);
    
    // SSGI SRV를 G-Buffer SRV Heap의 4번째 슬롯에 직접 생성 (index 4)
    D3D12_CPU_DESCRIPTOR_HANDLE gbufferSrvHandle = m_GBufferSRVHeap->GetCPUDescriptorHandleForHeapStart();
    gbufferSrvHandle.ptr += 4 * m_GBufferSRVDescriptorSize;
    m_Device->CreateShaderResourceView(m_SSGIBuffer.Get(), nullptr, gbufferSrvHandle);

    // SSGI UAV를 G-Buffer SRV Heap의 6번째 슬롯에 직접 생성 (index 6)
    // index 5는 ShadowMap SRV 용도
    D3D12_CPU_DESCRIPTOR_HANDLE gbufferUavHandle = m_GBufferSRVHeap->GetCPUDescriptorHandleForHeapStart();
    gbufferUavHandle.ptr += 6 * m_GBufferSRVDescriptorSize; // 5 → 6
    m_Device->CreateUnorderedAccessView(m_SSGIBuffer.Get(), nullptr, nullptr, gbufferUavHandle);

    // 이전 프레임 SSGI Output 버퍼 생성 (Temporal Filter용)
    m_Device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &ssgiDesc,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,  // SRV로 읽기만 함
        &clearValue,
        IID_PPV_ARGS(&m_SSGIPreviousBuffer)
    );

    // 이전 프레임 SSGI SRV를 G-Buffer SRV Heap의 7번째 슬롯에 생성 (index 7)
    D3D12_CPU_DESCRIPTOR_HANDLE gbufferPreviousSrvHandle = m_GBufferSRVHeap->GetCPUDescriptorHandleForHeapStart();
    gbufferPreviousSrvHandle.ptr += 7 * m_GBufferSRVDescriptorSize;
    m_Device->CreateShaderResourceView(m_SSGIPreviousBuffer.Get(), nullptr, gbufferPreviousSrvHandle);
}

D3D12_CPU_DESCRIPTOR_HANDLE RendererCore::GetSSGIRTVHandle() const {
    return m_SSGIRTVHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_CPU_DESCRIPTOR_HANDLE RendererCore::GetSSGISRVHandle() const {
    return m_SSGISRVHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_GPU_DESCRIPTOR_HANDLE RendererCore::GetSSGIUAVHandle() const {
    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_SSGISRVHeap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += m_SSGISRVDescriptorSize; // UAV는 SRV 다음 슬롯
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE RendererCore::GetSSGISRVHandleFromGBufferHeap() const {
    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_GBufferSRVHeap->GetGPUDescriptorHandleForHeapStart();
    // 0-3: G-Buffer SRV, 4: SSGI SRV, 5: ShadowMap SRV, 6: SSGI UAV
    handle.ptr += 4 * m_GBufferSRVDescriptorSize; // SSGI SRV는 index 4
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE RendererCore::GetSSGIUAVHandleFromGBufferHeap() const {
    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_GBufferSRVHeap->GetGPUDescriptorHandleForHeapStart();
    // 0-3: G-Buffer SRV, 4: SSGI SRV, 5: ShadowMap SRV, 6: SSGI UAV
    handle.ptr += 6 * m_GBufferSRVDescriptorSize; // SSGI UAV는 index 6
    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE RendererCore::GetSSGIPreviousSRVHandleFromGBufferHeap() const {
    D3D12_GPU_DESCRIPTOR_HANDLE handle = m_GBufferSRVHeap->GetGPUDescriptorHandleForHeapStart();
    // 0-3: G-Buffer SRV, 4: SSGI SRV, 5: ShadowMap SRV, 6: SSGI UAV, 7: SSGI Previous SRV
    handle.ptr += 7 * m_GBufferSRVDescriptorSize; // 이전 프레임 SSGI SRV는 index 7
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE RendererCore::GetGBufferRTVHandle(int index) const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_GBufferRTVHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += index * m_GBufferRTVDescriptorSize;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE RendererCore::GetGBufferSRVHandle(int index) const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_GBufferSRVHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += index * m_GBufferSRVDescriptorSize;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE RendererCore::GetLightingRTVHandle() const {
    return m_LightingRTVHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_CPU_DESCRIPTOR_HANDLE RendererCore::GetCurrentBackBufferRTV() const {
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_RTVHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += m_FrameIndex * m_RTVDescriptorSize;
    return handle;
}

void RendererCore::CreateFence() {
    m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence));

    m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_FenceEvent) {
        throw std::runtime_error("Failed to create fence event");
    }
}

void RendererCore::BeginFrame() {
    WaitForFrameResource(m_FrameIndex);

    FrameResource& frameResource = m_FrameResources[m_FrameIndex];
    frameResource.CommandAllocator->Reset();
    m_CommandList->Reset(frameResource.CommandAllocator.Get(), nullptr);

    // Transition back buffer to render target state (for lighting pass)
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_RenderTargets[m_FrameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    m_CommandList->ResourceBarrier(1, &barrier);

    // Set viewport and scissor rect (used by both passes)
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(m_Width), static_cast<float>(m_Height), 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, static_cast<LONG>(m_Width), static_cast<LONG>(m_Height) };

    m_CommandList->RSSetViewports(1, &viewport);
    m_CommandList->RSSetScissorRects(1, &scissorRect);
}

void RendererCore::EndFrame() {
    // Transition back buffer to present state (if it was used as render target)
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_RenderTargets[m_FrameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

    m_CommandList->ResourceBarrier(1, &barrier);
    m_CommandList->Close();

    // Execute command list
    ID3D12CommandList* commandLists[] = { m_CommandList.Get() };
    m_CommandQueue->ExecuteCommandLists(1, commandLists);
}

void RendererCore::Present() {
    m_SwapChain->Present(0, 0);
    MoveToNextFrame();
}

void RendererCore::WaitForGPU() {
    if (!m_CommandQueue || !m_Fence || !m_FenceEvent) {
        return;
    }

    const uint64_t fenceValue = m_NextFenceValue++;
    m_CommandQueue->Signal(m_Fence.Get(), fenceValue);

    if (m_Fence->GetCompletedValue() < fenceValue) {
        m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent);
        WaitForSingleObject(m_FenceEvent, INFINITE);
    }

    for (uint32_t i = 0; i < FrameCount; ++i) {
        m_FrameResources[i].FenceValue = fenceValue;
        m_FenceValues[i] = fenceValue;
    }
}

void RendererCore::FlushCommandQueue() {
    // 현재 프레임의 fence 값을 증가시켜 명령 큐에 Signal 추가
    const uint64_t newFenceValue = m_NextFenceValue++;
    
    // 명령 큐에 Signal 추가 (GPU가 이전 모든 명령을 완료할 때까지 대기)
    m_CommandQueue->Signal(m_Fence.Get(), newFenceValue);
    
    // GPU가 모든 명령을 완료할 때까지 대기
    if (m_Fence->GetCompletedValue() < newFenceValue) {
        m_Fence->SetEventOnCompletion(newFenceValue, m_FenceEvent);
        WaitForSingleObject(m_FenceEvent, INFINITE);
    }
    
    // Fence 값 업데이트
    m_FrameResources[m_FrameIndex].FenceValue = newFenceValue;
    m_FenceValues[m_FrameIndex] = newFenceValue;
}

void RendererCore::ExecuteCommandListAndWait() {
    m_CommandList->Close();

    ID3D12CommandList* commandLists[] = { m_CommandList.Get() };
    m_CommandQueue->ExecuteCommandLists(1, commandLists);

    FlushCommandQueue();
}

void RendererCore::ResetCommandList() {
    WaitForFrameResource(m_FrameIndex);

    FrameResource& frameResource = m_FrameResources[m_FrameIndex];
    frameResource.CommandAllocator->Reset();
    m_CommandList->Reset(frameResource.CommandAllocator.Get(), nullptr);
}

void RendererCore::WaitForFrameResource(uint32_t frameIndex) {
    const uint64_t fenceValue = m_FrameResources[frameIndex].FenceValue;
    if (fenceValue == 0 || m_Fence->GetCompletedValue() >= fenceValue) {
        return;
    }

    m_Fence->SetEventOnCompletion(fenceValue, m_FenceEvent);
    WaitForSingleObject(m_FenceEvent, INFINITE);
}

void RendererCore::MoveToNextFrame() {
    const uint32_t submittedFrameIndex = m_FrameIndex;
    const uint64_t fenceValue = m_NextFenceValue++;
    m_CommandQueue->Signal(m_Fence.Get(), fenceValue);
    m_FrameResources[submittedFrameIndex].FenceValue = fenceValue;
    m_FenceValues[submittedFrameIndex] = fenceValue;

    m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();

    WaitForFrameResource(m_FrameIndex);
}

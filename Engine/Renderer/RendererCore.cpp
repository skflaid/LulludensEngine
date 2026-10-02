#include "RendererCore.h"
#include <stdexcept>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

RendererCore::RendererCore()
    : m_FrameIndex(0)
    , m_RTVDescriptorSize(0)
    , m_GBufferRTVDescriptorSize(0)
    , m_GBufferSRVDescriptorSize(0)
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
            IID_PPV_ARGS(&m_CommandAllocators[i])
        );
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
    depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
    depthStencilDesc.SampleDesc.Count = 1;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;

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

    m_Device->CreateDepthStencilView(m_DepthStencil.Get(), nullptr, m_DSVHeap->GetCPUDescriptorHandleForHeapStart());
}

void RendererCore::CreateGBuffer() {
    // G-Buffer RTV Heap 생성 (4개 RT: Position, Normal, Albedo, Material)
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 4;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    m_Device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_GBufferRTVHeap));
    m_GBufferRTVDescriptorSize = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    // G-Buffer SRV Heap 생성
 // 0: Position SRV
 // 1: Normal   SRV
 // 2: Albedo   SRV
 // 3: Material SRV
 // 4: SSGI Filtered   SRV
 // 5: Shadow   SRV
 // 6: SSGI  Raw   UAV
 // 7: SSGI Previous SRV (이전 프레임)
 // 8: SSGI Raw SRV
 // 9: SSGI Filtered UAV
 // 10+: 텍스처 SRV들 (알비도, 노말맵 등)
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.NumDescriptors = 10 + 256; // 텍스처를 위한 공간 추가 (최대 256개)
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

    auto createGBufferTexture =
        [&](GpuTexture& texture, DXGI_FORMAT format)
        {
            gbufferDesc.Format = format;

            D3D12_CLEAR_VALUE clearValue = {};
            clearValue.Format = format;
            clearValue.Color[0] = 0.0f;
            clearValue.Color[1] = 0.0f;
            clearValue.Color[2] = 0.0f;
            clearValue.Color[3] = 0.0f;

            ComPtr<ID3D12Resource> resource;

            const HRESULT result =
                m_Device->CreateCommittedResource(
                    &heapProps,
                    D3D12_HEAP_FLAG_NONE,
                    &gbufferDesc,
                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                    &clearValue,
                    IID_PPV_ARGS(&resource));

            if (FAILED(result))
            {
                throw std::runtime_error(
                    "Failed to create G-Buffer texture.");
            }

            texture.Initialize(
                std::move(resource),
                D3D12_RESOURCE_STATE_RENDER_TARGET);

            m_Device->CreateRenderTargetView(
                texture.Get(),
                nullptr,
                rtvHandle);

            m_Device->CreateShaderResourceView(
                texture.Get(),
                nullptr,
                srvHandle);

            rtvHandle.ptr += m_GBufferRTVDescriptorSize;
            srvHandle.ptr += m_GBufferSRVDescriptorSize;
        };

    createGBufferTexture(
        m_GBufferPosition,
        DXGI_FORMAT_R32G32B32A32_FLOAT);

    createGBufferTexture(
        m_GBufferNormal,
        DXGI_FORMAT_R16G16B16A16_FLOAT);

    createGBufferTexture(
        m_GBufferAlbedo,
        DXGI_FORMAT_R8G8B8A8_UNORM);

    createGBufferTexture(
        m_GBufferMaterial,
        DXGI_FORMAT_R8G8B8A8_UNORM);
}

void RendererCore::CreateSSGIBuffer()
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = m_Width;
    desc.Height = m_Height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Flags =
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    auto createTexture =
        [&](GpuTexture& texture,
            D3D12_RESOURCE_STATES initialState)
        {
            ComPtr<ID3D12Resource> resource;

            const HRESULT result =
                m_Device->CreateCommittedResource(
                    &heapProps,
                    D3D12_HEAP_FLAG_NONE,
                    &desc,
                    initialState,
                    nullptr,
                    IID_PPV_ARGS(&resource));

            if (FAILED(result))
            {
                throw std::runtime_error(
                    "Failed to create SSGI texture.");
            }

            texture.Initialize(
                std::move(resource),
                initialState);
        };

    createTexture(
        m_SSGIRaw,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    createTexture(
        m_SSGIFiltered,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    createTexture(
        m_SSGIPrevious,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    auto getCPUHandle =
        [&](UINT index)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE handle =
                m_GBufferSRVHeap->
                GetCPUDescriptorHandleForHeapStart();

            handle.ptr +=
                index * m_GBufferSRVDescriptorSize;

            return handle;
        };

    // 슬롯 4: Lighting이 읽는 최종 결과
    m_Device->CreateShaderResourceView(
        m_SSGIFiltered.Get(),
        nullptr,
        getCPUHandle(4));

    // 슬롯 6: SSGI 패스 Raw 출력
    m_Device->CreateUnorderedAccessView(
        m_SSGIRaw.Get(),
        nullptr,
        nullptr,
        getCPUHandle(6));

    // 슬롯 7: 이전 프레임 결과
    m_Device->CreateShaderResourceView(
        m_SSGIPrevious.Get(),
        nullptr,
        getCPUHandle(7));

    // 슬롯 8: Denoise 입력
    m_Device->CreateShaderResourceView(
        m_SSGIRaw.Get(),
        nullptr,
        getCPUHandle(8));

    // 슬롯 9: Denoise 출력
    m_Device->CreateUnorderedAccessView(
        m_SSGIFiltered.Get(),
        nullptr,
        nullptr,
        getCPUHandle(9));
}

D3D12_GPU_DESCRIPTOR_HANDLE
RendererCore::GetSSGIRawSRVHandleFromGBufferHeap() const
{
    auto handle =
        m_GBufferSRVHeap->
        GetGPUDescriptorHandleForHeapStart();

    handle.ptr +=
        8 * m_GBufferSRVDescriptorSize;

    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE
RendererCore::GetSSGIRawUAVHandleFromGBufferHeap() const
{
    auto handle =
        m_GBufferSRVHeap->
        GetGPUDescriptorHandleForHeapStart();

    handle.ptr +=
        6 * m_GBufferSRVDescriptorSize;

    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE
RendererCore::GetSSGIFilteredUAVHandleFromGBufferHeap() const
{
    auto handle =
        m_GBufferSRVHeap->
        GetGPUDescriptorHandleForHeapStart();

    handle.ptr +=
        9 * m_GBufferSRVDescriptorSize;

    return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE
RendererCore::GetSSGIPreviousSRVHandleFromGBufferHeap() const
{
    auto handle =
        m_GBufferSRVHeap->
        GetGPUDescriptorHandleForHeapStart();

    handle.ptr +=
        7 * m_GBufferSRVDescriptorSize;

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

void RendererCore::CreateFence() {
    m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence));

    m_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_FenceEvent) {
        throw std::runtime_error("Failed to create fence event");
    }
}

void RendererCore::BeginFrame() {
    m_CommandAllocators[m_FrameIndex]->Reset();
    m_CommandList->Reset(m_CommandAllocators[m_FrameIndex].Get(), nullptr);

    m_CommandContext.Begin(
        m_CommandList.Get());

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

    m_CommandContext.FlushResourceBarriers();
    m_CommandList->Close();

    // Execute command list
    ID3D12CommandList* commandLists[] = { m_CommandList.Get() };
    m_CommandQueue->ExecuteCommandLists(1, commandLists);
}

void RendererCore::Present() {
    m_SwapChain->Present(1, 0);
    MoveToNextFrame();
}

void RendererCore::WaitForGPU() {
    m_CommandQueue->Signal(m_Fence.Get(), m_FenceValues[m_FrameIndex]);
    m_Fence->SetEventOnCompletion(m_FenceValues[m_FrameIndex], m_FenceEvent);
    WaitForSingleObject(m_FenceEvent, INFINITE);
}

void RendererCore::FlushCommandQueue() {
    // 현재 프레임의 fence 값을 증가시켜 명령 큐에 Signal 추가
    const uint64_t currentFenceValue = m_FenceValues[m_FrameIndex];
    const uint64_t newFenceValue = currentFenceValue + 1;
    
    // 명령 큐에 Signal 추가 (GPU가 이전 모든 명령을 완료할 때까지 대기)
    m_CommandQueue->Signal(m_Fence.Get(), newFenceValue);
    
    // GPU가 모든 명령을 완료할 때까지 대기
    if (m_Fence->GetCompletedValue() < newFenceValue) {
        m_Fence->SetEventOnCompletion(newFenceValue, m_FenceEvent);
        WaitForSingleObject(m_FenceEvent, INFINITE);
    }
    
    // Fence 값 업데이트
    m_FenceValues[m_FrameIndex] = newFenceValue;
}

void RendererCore::MoveToNextFrame() {
    const uint64_t currentFenceValue = m_FenceValues[m_FrameIndex];
    m_CommandQueue->Signal(m_Fence.Get(), currentFenceValue);

    m_FrameIndex = m_SwapChain->GetCurrentBackBufferIndex();

    if (m_Fence->GetCompletedValue() < m_FenceValues[m_FrameIndex]) {
        m_Fence->SetEventOnCompletion(m_FenceValues[m_FrameIndex], m_FenceEvent);
        WaitForSingleObject(m_FenceEvent, INFINITE);
    }

    m_FenceValues[m_FrameIndex] = currentFenceValue + 1;
}

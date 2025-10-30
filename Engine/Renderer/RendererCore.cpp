#include "RendererCore.h"
#include <stdexcept>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

RendererCore::RendererCore()
    : m_FrameIndex(0)
    , m_RTVDescriptorSize(0)
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

    // Transition render target to render target state
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_RenderTargets[m_FrameIndex].Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

    m_CommandList->ResourceBarrier(1, &barrier);

    // Clear render target and depth stencil
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_RTVHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += m_FrameIndex * m_RTVDescriptorSize;

    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = m_DSVHeap->GetCPUDescriptorHandleForHeapStart();

    const float clearColor[] = { 0.2f, 0.2f, 0.2f, 1.0f };
    m_CommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_CommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    m_CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

    // Set viewport and scissor rect
    D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(m_Width), static_cast<float>(m_Height), 0.0f, 1.0f };
    D3D12_RECT scissorRect = { 0, 0, static_cast<LONG>(m_Width), static_cast<LONG>(m_Height) };

    m_CommandList->RSSetViewports(1, &viewport);
    m_CommandList->RSSetScissorRects(1, &scissorRect);
}

void RendererCore::EndFrame() {
    // Transition render target to present state
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
    m_SwapChain->Present(1, 0);
    MoveToNextFrame();
}

void RendererCore::WaitForGPU() {
    m_CommandQueue->Signal(m_Fence.Get(), m_FenceValues[m_FrameIndex]);
    m_Fence->SetEventOnCompletion(m_FenceValues[m_FrameIndex], m_FenceEvent);
    WaitForSingleObject(m_FenceEvent, INFINITE);
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

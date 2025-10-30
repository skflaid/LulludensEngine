#pragma once
#include <d3d12.h>
#include <dxgi1_6.h>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <memory>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

class RendererCore {
public:
    RendererCore();
    ~RendererCore();

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height);
    void Shutdown();

    void BeginFrame();
    void EndFrame();
    void Present();

    ID3D12Device* GetDevice() const { return m_Device.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return m_CommandList.Get(); }

    uint32_t GetWidth() const { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }

private:
    void CreateDevice();
    void CreateCommandQueue();
    void CreateSwapChain(HWND hwnd);
    void CreateRenderTargetViews();
    void CreateDepthStencilBuffer();
    void CreateFence();

    void WaitForGPU();
    void MoveToNextFrame();

private:
    static const uint32_t FrameCount = 2;

    // Core D3D12 objects
    ComPtr<ID3D12Device> m_Device;
    ComPtr<IDXGIFactory4> m_Factory;
    ComPtr<ID3D12CommandQueue> m_CommandQueue;
    ComPtr<IDXGISwapChain3> m_SwapChain;
    ComPtr<ID3D12CommandAllocator> m_CommandAllocators[FrameCount];
    ComPtr<ID3D12GraphicsCommandList> m_CommandList;

    // Render targets
    ComPtr<ID3D12Resource> m_RenderTargets[FrameCount];
    ComPtr<ID3D12DescriptorHeap> m_RTVHeap;
    uint32_t m_RTVDescriptorSize;

    // Depth stencil
    ComPtr<ID3D12Resource> m_DepthStencil;
    ComPtr<ID3D12DescriptorHeap> m_DSVHeap;

    // Synchronization objects
    ComPtr<ID3D12Fence> m_Fence;
    uint64_t m_FenceValues[FrameCount];
    HANDLE m_FenceEvent;

    uint32_t m_FrameIndex;
    uint32_t m_Width;
    uint32_t m_Height;
};

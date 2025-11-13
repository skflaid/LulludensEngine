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
    uint32_t GetFrameIndex() const { return m_FrameIndex; }
    ID3D12DescriptorHeap* GetRTVHeap() const { return m_RTVHeap.Get(); }
    ID3D12DescriptorHeap* GetDSVHeap() const { return m_DSVHeap.Get(); }
    uint32_t GetRTVDescriptorSize() const { return m_RTVDescriptorSize; }

    // G-Buffer access
    ID3D12Resource* GetGBufferPosition() const { return m_GBufferPosition.Get(); }
    ID3D12Resource* GetGBufferNormal() const { return m_GBufferNormal.Get(); }
    ID3D12Resource* GetGBufferAlbedo() const { return m_GBufferAlbedo.Get(); }
    ID3D12Resource* GetGBufferMaterial() const { return m_GBufferMaterial.Get(); }
    ID3D12DescriptorHeap* GetGBufferRTVHeap() const { return m_GBufferRTVHeap.Get(); }
    ID3D12DescriptorHeap* GetGBufferSRVHeap() const { return m_GBufferSRVHeap.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetGBufferRTVHandle(int index) const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetGBufferSRVHandle(int index) const;

    // SSGI access
    ID3D12Resource* GetSSGIBuffer() const { return m_SSGIBuffer.Get(); }
    ID3D12DescriptorHeap* GetSSGIRTVHeap() const { return m_SSGIRTVHeap.Get(); }
    ID3D12DescriptorHeap* GetSSGISRVHeap() const { return m_SSGISRVHeap.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetSSGIRTVHandle() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetSSGISRVHandle() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetSSGIUAVHandle() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetSSGIUAVHandleFromGBufferHeap() const;

private:
    void CreateDevice();
    void CreateCommandQueue();
    void CreateSwapChain(HWND hwnd);
    void CreateRenderTargetViews();
    void CreateDepthStencilBuffer();
    void CreateGBuffer();
    void CreateSSGIBuffer();
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

    // G-Buffer
    ComPtr<ID3D12Resource> m_GBufferPosition;
    ComPtr<ID3D12Resource> m_GBufferNormal;
    ComPtr<ID3D12Resource> m_GBufferAlbedo;
    ComPtr<ID3D12Resource> m_GBufferMaterial;
    ComPtr<ID3D12DescriptorHeap> m_GBufferRTVHeap;
    ComPtr<ID3D12DescriptorHeap> m_GBufferSRVHeap;
    uint32_t m_GBufferRTVDescriptorSize;
    uint32_t m_GBufferSRVDescriptorSize;

    // SSGI
    ComPtr<ID3D12Resource> m_SSGIBuffer;
    ComPtr<ID3D12DescriptorHeap> m_SSGIRTVHeap;
    ComPtr<ID3D12DescriptorHeap> m_SSGISRVHeap;
    uint32_t m_SSGIRTVDescriptorSize;
    uint32_t m_SSGISRVDescriptorSize;

    // Synchronization objects
    ComPtr<ID3D12Fence> m_Fence;
    uint64_t m_FenceValues[FrameCount];
    HANDLE m_FenceEvent;

    uint32_t m_FrameIndex;
    uint32_t m_Width;
    uint32_t m_Height;
};

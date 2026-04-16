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

    // D3D12 장치, 스왑체인, G-Buffer, 오프스크린 렌더 타깃을 생성한다.
    bool Initialize(HWND hwnd, uint32_t width, uint32_t height);
    // GPU 대기 후 코어 자원을 정리한다.
    void Shutdown();

    // 프레임 시작 시 명령 리스트를 리셋하고 백버퍼를 렌더 타깃 상태로 바꾼다.
    void BeginFrame();
    // 프레임 종료 시 백버퍼를 present 상태로 돌리고 명령 리스트를 닫는다.
    void EndFrame();
    // 스왑체인을 present하고 다음 프레임 인덱스로 넘어간다.
    void Present();
    
    // GPU 동기화 - 모든 명령 실행 완료 대기
    void FlushCommandQueue();
    // 중간 패스에서 명령 리스트를 제출하고 GPU 완료까지 기다린다.
    void ExecuteCommandListAndWait();
    // 중간 제출 뒤 이어서 명령을 기록할 수 있게 리스트를 다시 연다.
    void ResetCommandList();

    ID3D12Device* GetDevice() const { return m_Device.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return m_CommandList.Get(); }
    ID3D12CommandQueue* GetCommandQueue() const { return m_CommandQueue.Get(); }
    ID3D12CommandAllocator* GetCommandAllocator(UINT index) const { return m_CommandAllocators[index].Get(); }

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
    // StyleTransfer 입력으로 쓰는 view-depth MRT.
    ID3D12Resource* GetGBufferDepth() const { return m_GBufferDepth.Get(); }
    ID3D12DescriptorHeap* GetGBufferRTVHeap() const { return m_GBufferRTVHeap.Get(); }
    ID3D12DescriptorHeap* GetGBufferSRVHeap() const { return m_GBufferSRVHeap.Get(); }
    uint32_t GetGBufferSRVDescriptorSize() const { return m_GBufferSRVDescriptorSize; }
    D3D12_CPU_DESCRIPTOR_HANDLE GetGBufferRTVHandle(int index) const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetGBufferSRVHandle(int index) const;
    // Lighting pass의 오프스크린 색상 출력.
    ID3D12Resource* GetLightingBuffer() const { return m_LightingBuffer.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetLightingRTVHandle() const;
    ID3D12Resource* GetCurrentBackBuffer() const { return m_RenderTargets[m_FrameIndex].Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentBackBufferRTV() const;

    // SSGI access
    ID3D12Resource* GetSSGIBuffer() const { return m_SSGIBuffer.Get(); }
    ID3D12Resource* GetSSGIPreviousBuffer() const { return m_SSGIPreviousBuffer.Get(); }
    ID3D12DescriptorHeap* GetSSGIRTVHeap() const { return m_SSGIRTVHeap.Get(); }
    ID3D12DescriptorHeap* GetSSGISRVHeap() const { return m_SSGISRVHeap.Get(); }
    D3D12_CPU_DESCRIPTOR_HANDLE GetSSGIRTVHandle() const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetSSGISRVHandle() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetSSGIUAVHandle() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetSSGISRVHandleFromGBufferHeap() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetSSGIUAVHandleFromGBufferHeap() const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetSSGIPreviousSRVHandleFromGBufferHeap() const;

private:
    void CreateDevice();
    void CreateCommandQueue();
    void CreateSwapChain(HWND hwnd);
    void CreateRenderTargetViews();
    void CreateDepthStencilBuffer();
    void CreateGBuffer();
    // Lighting 이후 후처리용 오프스크린 컬러 버퍼를 만든다.
    void CreateLightingBuffer();
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
    // DirectML 입력용 depth MRT.
    ComPtr<ID3D12Resource> m_GBufferDepth;
    ComPtr<ID3D12DescriptorHeap> m_GBufferRTVHeap;
    ComPtr<ID3D12DescriptorHeap> m_GBufferSRVHeap;
    uint32_t m_GBufferRTVDescriptorSize;
    uint32_t m_GBufferSRVDescriptorSize;
    // Lighting 결과를 백버퍼 대신 임시로 저장하는 버퍼.
    ComPtr<ID3D12Resource> m_LightingBuffer;
    ComPtr<ID3D12DescriptorHeap> m_LightingRTVHeap;
    uint32_t m_LightingRTVDescriptorSize;

    // SSGI
    ComPtr<ID3D12Resource> m_SSGIBuffer;
    ComPtr<ID3D12Resource> m_SSGIPreviousBuffer;  // 이전 프레임 SSGI Output
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

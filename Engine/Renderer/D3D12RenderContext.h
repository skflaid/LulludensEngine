#pragma once

#include "Renderer/FrameResource.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <vector>
#include <wrl/client.h>

class D3D12RenderContext {
public:
    ID3D12Device* GetDevice() const { return m_Device.Get(); }
    ID3D12CommandQueue* GetCommandQueue() const { return m_CommandQueue.Get(); }
    ID3D12GraphicsCommandList* GetCommandList() const { return m_CommandList.Get(); }
    ID3D12Fence* GetFence() const { return m_Fence.Get(); }

    FrameResource& GetFrameResource(unsigned int index) { return m_FrameResources[index]; }
    const FrameResource& GetFrameResource(unsigned int index) const { return m_FrameResources[index]; }

private:
    Microsoft::WRL::ComPtr<ID3D12Device> m_Device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_SwapChain;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_CommandList;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence;
    std::vector<FrameResource> m_FrameResources;
};


#pragma once

#include <d3d12.h>
#include <cstdint>
#include <wrl/client.h>

static constexpr uint32_t RendererFrameCount = 3;

struct FrameResource {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> CommandAllocator;
    Microsoft::WRL::ComPtr<ID3D12Resource> ObjectConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> MaterialConstantBuffer;
    Microsoft::WRL::ComPtr<ID3D12Resource> PassConstantBuffer;
    uint64_t FenceValue = 0;
};

#pragma once

#include "GpuResource.h"

class GpuTexture final : public GpuResource
{
public:
    GpuTexture() = default;

    void Initialize(
        Microsoft::WRL::ComPtr<ID3D12Resource> resource,
        D3D12_RESOURCE_STATES initialState)
    {
        SetResource(
            std::move(resource),
            initialState);
    }
};
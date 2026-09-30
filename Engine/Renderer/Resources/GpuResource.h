#pragma once

#include <d3d12.h>
#include <wrl/client.h>

#include <utility>

class GpuResource
{
public:
    virtual ~GpuResource() = default;

    GpuResource(const GpuResource&) = delete;
    GpuResource& operator=(const GpuResource&) = delete;

    GpuResource(GpuResource&&) = default;
    GpuResource& operator=(GpuResource&&) = default;

    ID3D12Resource* Get() const noexcept
    {
        return m_Resource.Get();
    }

    D3D12_RESOURCE_STATES GetCurrentState() const noexcept
    {
        return m_CurrentState;
    }

    void SetCurrentState(
        D3D12_RESOURCE_STATES state) noexcept
    {
        m_CurrentState = state;
    }

    bool IsValid() const noexcept
    {
        return m_Resource != nullptr;
    }

protected:
    GpuResource() = default;

    void SetResource(
        Microsoft::WRL::ComPtr<ID3D12Resource> resource,
        D3D12_RESOURCE_STATES initialState)
    {
        m_Resource = std::move(resource);
        m_CurrentState = initialState;
    }

    Microsoft::WRL::ComPtr<ID3D12Resource> m_Resource;

    D3D12_RESOURCE_STATES m_CurrentState =
        D3D12_RESOURCE_STATE_COMMON;
};
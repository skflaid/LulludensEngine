#pragma once

#include "../../../Common/directsr.h"
#include <d3d12.h>
#include <dxgi.h>
#include <string>
#include <vector>
#include <wrl/client.h>

class DirectSRUpscaler {
public:
    struct VariantInfo {
        UINT Index = UINT_MAX;
        DSR_SUPERRES_VARIANT_DESC Desc = {};
    };

    struct InitializeDesc {
        ID3D12Device* Device = nullptr;
        ID3D12CommandQueue* CommandQueue = nullptr;
        DXGI_FORMAT TargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        DXGI_FORMAT SourceColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        DXGI_FORMAT SourceDepthFormat = DXGI_FORMAT_R32_FLOAT;
        DSR_OPTIMIZATION_TYPE OptimizationType = DSR_OPTIMIZATION_TYPE_BALANCED;
        DSR_SUPERRES_CREATE_ENGINE_FLAGS CreateFlags = DSR_SUPERRES_CREATE_ENGINE_FLAG_NONE;
        UINT PreferredVariantIndex = UINT_MAX;
    };

    struct UpscaleDesc {
        ID3D12Resource* SourceColorTexture = nullptr;
        ID3D12Resource* SourceDepthTexture = nullptr;
        ID3D12Resource* MotionVectorsTexture = nullptr;
        ID3D12Resource* TargetTexture = nullptr;

        D3D12_RECT SourceColorRegion = {};
        D3D12_RECT SourceDepthRegion = {};
        D3D12_RECT MotionVectorsRegion = {};
        D3D12_RECT TargetRegion = {};

        DXGI_FORMAT SourceColorFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT SourceDepthFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT TargetFormat = DXGI_FORMAT_UNKNOWN;

        DSR_FLOAT2 MotionVectorScale = { 1.0f, 1.0f };
        DSR_FLOAT2 CameraJitter = { 0.0f, 0.0f };
        float ExposureScale = 1.0f;
        float PreExposure = 1.0f;
        float Sharpness = 0.0f;
        float CameraNear = 0.1f;
        float CameraFar = 1000.0f;
        float CameraFovAngleVert = 0.785398163f;
        float TimeDeltaInSeconds = 0.0f;
        bool ResetHistory = false;
    };

    DirectSRUpscaler() = default;
    ~DirectSRUpscaler();

    DirectSRUpscaler(const DirectSRUpscaler&) = delete;
    DirectSRUpscaler& operator=(const DirectSRUpscaler&) = delete;

    bool Initialize(const InitializeDesc& desc);
    void Shutdown();

    HRESULT Upscale(UINT targetWidth, UINT targetHeight, const UpscaleDesc& desc);
    HRESULT QuerySourceSettings(
        UINT targetWidth,
        UINT targetHeight,
        DXGI_FORMAT targetFormat,
        DSR_SUPERRES_SOURCE_SETTINGS& sourceSettings) const;

    bool IsInitialized() const { return m_Device != nullptr && m_Table.pfnDSRExSuperResExecuteUpscaler != nullptr; }
    ID3D12Resource* GetOutputTexture() const { return m_OutputTexture.Get(); }
    const DSR_SUPERRES_VARIANT_DESC& GetSelectedVariantDesc() const { return m_SelectedVariantDesc; }
    UINT GetSelectedVariantIndex() const { return m_SelectedVariantIndex; }
    const std::vector<VariantInfo>& GetAvailableVariants() const { return m_AvailableVariants; }
    const DSR_SUPERRES_SOURCE_SETTINGS& GetLastSourceSettings() const { return m_LastSourceSettings; }
    const std::string& GetLastError() const { return m_LastError; }

private:
    struct EngineKey {
        UINT TargetWidth = 0;
        UINT TargetHeight = 0;
        UINT MaxSourceWidth = 0;
        UINT MaxSourceHeight = 0;
        DXGI_FORMAT TargetFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT SourceColorFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT SourceDepthFormat = DXGI_FORMAT_UNKNOWN;

        bool operator==(const EngineKey& other) const;
        bool operator!=(const EngineKey& other) const { return !(*this == other); }
    };

    bool LoadDirectSR();
    bool SelectVariant(UINT preferredVariantIndex);
    HRESULT EnsureEngine(const EngineKey& key);
    HRESULT EnsureOutputTexture(UINT targetWidth, UINT targetHeight, DXGI_FORMAT targetFormat);
    void DestroyEngine();
    void DestroyOutputTexture();
    void ResetState();
    void SetLastError(const std::string& message);

    static D3D12_RECT FullRect(UINT width, UINT height);
    static D3D12_RECT FullRect(ID3D12Resource* resource);
    static bool IsEmptyRect(const D3D12_RECT& rect);
    static bool GetTextureSize(ID3D12Resource* resource, UINT& width, UINT& height);
    static DXGI_FORMAT TextureFormat(ID3D12Resource* resource);
    static std::string HResultToString(HRESULT hr);

private:
    Microsoft::WRL::ComPtr<ID3D12Device> m_Device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CommandQueue;

    HMODULE m_DirectSRModule = nullptr;
    DSR_EX_FUNCTION_TABLE_1_0 m_Table = {};
    UINT m_SelectedVariantIndex = UINT_MAX;
    DSR_SUPERRES_VARIANT_DESC m_SelectedVariantDesc = {};
    std::vector<VariantInfo> m_AvailableVariants;
    DSR_SUPERRES_SOURCE_SETTINGS m_LastSourceSettings = {};

    DXGI_FORMAT m_DefaultTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT m_DefaultSourceColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    DXGI_FORMAT m_DefaultSourceDepthFormat = DXGI_FORMAT_R32_FLOAT;
    DSR_OPTIMIZATION_TYPE m_OptimizationType = DSR_OPTIMIZATION_TYPE_BALANCED;
    DSR_SUPERRES_CREATE_ENGINE_FLAGS m_CreateFlags = DSR_SUPERRES_CREATE_ENGINE_FLAG_NONE;

    DSRExSuperResEngineHandle m_Engine = nullptr;
    DSRExSuperResUpscalerHandle m_Upscaler = nullptr;
    EngineKey m_EngineKey = {};
    bool m_HasEngineKey = false;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_OutputTexture;
    UINT m_OutputWidth = 0;
    UINT m_OutputHeight = 0;
    DXGI_FORMAT m_OutputFormat = DXGI_FORMAT_UNKNOWN;
    std::string m_LastError;
};

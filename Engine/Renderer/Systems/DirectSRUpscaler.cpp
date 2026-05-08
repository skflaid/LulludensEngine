#include "DirectSRUpscaler.h"

#include <cstdio>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <windows.h>

namespace {
std::wstring GetExecutableDirectory()
{
    wchar_t path[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length == MAX_PATH) {
        return L".";
    }

    return std::filesystem::path(path).parent_path().wstring();
}

HMODULE TryLoadLibrary(const std::wstring& path)
{
    return LoadLibraryW(path.c_str());
}
}

DirectSRUpscaler::~DirectSRUpscaler()
{
    Shutdown();
}

bool DirectSRUpscaler::Initialize(const InitializeDesc& desc)
{
    Shutdown();

    if (!desc.Device || !desc.CommandQueue) {
        SetLastError("DirectSRUpscaler::Initialize requires a valid D3D12 device and command queue.");
        return false;
    }

    m_Device = desc.Device;
    m_CommandQueue = desc.CommandQueue;
    m_DefaultTargetFormat = desc.TargetFormat;
    m_DefaultSourceColorFormat = desc.SourceColorFormat;
    m_DefaultSourceDepthFormat = desc.SourceDepthFormat;
    m_OptimizationType = desc.OptimizationType;
    m_CreateFlags = desc.CreateFlags;

    if (!LoadDirectSR()) {
        ResetState();
        return false;
    }

    if (!SelectVariant(desc.PreferredVariantIndex)) {
        ResetState();
        return false;
    }

    m_LastError.clear();
    return true;
}

void DirectSRUpscaler::Shutdown()
{
    DestroyOutputTexture();
    DestroyEngine();

    if (m_DirectSRModule) {
        FreeLibrary(m_DirectSRModule);
        m_DirectSRModule = nullptr;
    }

    ResetState();
}

HRESULT DirectSRUpscaler::Upscale(UINT targetWidth, UINT targetHeight, const UpscaleDesc& desc)
{
    if (!IsInitialized()) {
        SetLastError("DirectSRUpscaler is not initialized.");
        return E_FAIL;
    }

    if (targetWidth == 0 || targetHeight == 0) {
        SetLastError("DirectSRUpscaler::Upscale received an invalid target resolution.");
        return E_INVALIDARG;
    }

    if (!desc.SourceColorTexture) {
        SetLastError("DirectSRUpscaler::Upscale requires SourceColorTexture.");
        return E_INVALIDARG;
    }

    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    if (!GetTextureSize(desc.SourceColorTexture, sourceWidth, sourceHeight)) {
        SetLastError("SourceColorTexture is not a valid 2D texture.");
        return E_INVALIDARG;
    }

    const DXGI_FORMAT targetFormat = desc.TargetFormat != DXGI_FORMAT_UNKNOWN
        ? desc.TargetFormat
        : (desc.TargetTexture ? TextureFormat(desc.TargetTexture) : m_DefaultTargetFormat);
    const DXGI_FORMAT sourceColorFormat = desc.SourceColorFormat != DXGI_FORMAT_UNKNOWN
        ? desc.SourceColorFormat
        : TextureFormat(desc.SourceColorTexture);
    const DXGI_FORMAT sourceDepthFormat = desc.SourceDepthFormat != DXGI_FORMAT_UNKNOWN
        ? desc.SourceDepthFormat
        : (desc.SourceDepthTexture ? TextureFormat(desc.SourceDepthTexture) : m_DefaultSourceDepthFormat);

    if (targetFormat == DXGI_FORMAT_UNKNOWN || sourceColorFormat == DXGI_FORMAT_UNKNOWN || sourceDepthFormat == DXGI_FORMAT_UNKNOWN) {
        SetLastError("DirectSRUpscaler::Upscale could not resolve the required texture formats.");
        return E_INVALIDARG;
    }

    EngineKey key = {};
    key.TargetWidth = targetWidth;
    key.TargetHeight = targetHeight;
    key.MaxSourceWidth = sourceWidth;
    key.MaxSourceHeight = sourceHeight;
    key.TargetFormat = targetFormat;
    key.SourceColorFormat = sourceColorFormat;
    key.SourceDepthFormat = sourceDepthFormat;

    const bool resetHistoryForNewEngine = !m_HasEngineKey || m_EngineKey != key;
    HRESULT hr = EnsureEngine(key);
    if (FAILED(hr)) {
        return hr;
    }

    ID3D12Resource* targetTexture = desc.TargetTexture;
    if (!targetTexture) {
        hr = EnsureOutputTexture(targetWidth, targetHeight, targetFormat);
        if (FAILED(hr)) {
            return hr;
        }
        targetTexture = m_OutputTexture.Get();
    }

    DSR_SUPERRES_UPSCALER_EXECUTE_PARAMETERS params = {};
    params.pTargetTexture = targetTexture;
    params.TargetRegion = IsEmptyRect(desc.TargetRegion) ? FullRect(targetWidth, targetHeight) : desc.TargetRegion;
    params.pSourceColorTexture = desc.SourceColorTexture;
    params.SourceColorRegion = IsEmptyRect(desc.SourceColorRegion) ? FullRect(desc.SourceColorTexture) : desc.SourceColorRegion;
    params.pSourceDepthTexture = desc.SourceDepthTexture;
    params.SourceDepthRegion = desc.SourceDepthTexture && IsEmptyRect(desc.SourceDepthRegion)
        ? FullRect(desc.SourceDepthTexture)
        : desc.SourceDepthRegion;
    params.pMotionVectorsTexture = desc.MotionVectorsTexture;
    params.MotionVectorsRegion = desc.MotionVectorsTexture && IsEmptyRect(desc.MotionVectorsRegion)
        ? FullRect(desc.MotionVectorsTexture)
        : desc.MotionVectorsRegion;
    params.MotionVectorScale = desc.MotionVectorScale;
    params.CameraJitter = desc.CameraJitter;
    params.ExposureScale = desc.ExposureScale;
    params.PreExposure = desc.PreExposure;
    params.Sharpness = desc.Sharpness;
    params.CameraNear = desc.CameraNear;
    params.CameraFar = desc.CameraFar;
    params.CameraFovAngleVert = desc.CameraFovAngleVert;
    params.pExposureScaleTexture = nullptr;
    params.pIgnoreHistoryMaskTexture = nullptr;
    params.IgnoreHistoryMaskRegion = {};
    params.pReactiveMaskTexture = nullptr;
    params.ReactiveMaskRegion = {};

    DSR_SUPERRES_UPSCALER_EXECUTE_FLAGS flags = DSR_SUPERRES_UPSCALER_EXECUTE_FLAG_NONE;
    if (desc.ResetHistory || resetHistoryForNewEngine) {
        flags = static_cast<DSR_SUPERRES_UPSCALER_EXECUTE_FLAGS>(flags | DSR_SUPERRES_UPSCALER_EXECUTE_FLAG_RESET_HISTORY);
    }

    hr = m_Table.pfnDSRExSuperResExecuteUpscaler(m_Upscaler, &params, desc.TimeDeltaInSeconds, flags);
    if (FAILED(hr)) {
        SetLastError("DirectSR ExecuteUpscaler failed with HRESULT " + HResultToString(hr) + ".");
        return hr;
    }

    m_LastError.clear();
    return S_OK;
}

HRESULT DirectSRUpscaler::QuerySourceSettings(
    UINT targetWidth,
    UINT targetHeight,
    DXGI_FORMAT targetFormat,
    DSR_SUPERRES_SOURCE_SETTINGS& sourceSettings) const
{
    sourceSettings = {};

    if (!IsInitialized()) {
        return E_FAIL;
    }

    if (targetWidth == 0 || targetHeight == 0 || targetFormat == DXGI_FORMAT_UNKNOWN) {
        return E_INVALIDARG;
    }

    DSR_SIZE targetSize = { targetWidth, targetHeight };
    return m_Table.pfnDSRExSuperResQuerySourceSettings(
        m_SelectedVariantIndex,
        m_Device.Get(),
        targetSize,
        targetFormat,
        m_OptimizationType,
        m_CreateFlags,
        &sourceSettings);
}

bool DirectSRUpscaler::EngineKey::operator==(const EngineKey& other) const
{
    return TargetWidth == other.TargetWidth
        && TargetHeight == other.TargetHeight
        && MaxSourceWidth == other.MaxSourceWidth
        && MaxSourceHeight == other.MaxSourceHeight
        && TargetFormat == other.TargetFormat
        && SourceColorFormat == other.SourceColorFormat
        && SourceDepthFormat == other.SourceDepthFormat;
}

bool DirectSRUpscaler::LoadDirectSR()
{
    m_DirectSRModule = TryLoadLibrary(L"DirectSR.dll");
    if (!m_DirectSRModule) {
        m_DirectSRModule = TryLoadLibrary(L".\\D3D12\\DirectSR.dll");
    }
    if (!m_DirectSRModule) {
        const std::filesystem::path executableDirectSR = std::filesystem::path(GetExecutableDirectory()) / L"D3D12" / L"DirectSR.dll";
        m_DirectSRModule = TryLoadLibrary(executableDirectSR.wstring());
    }

    if (!m_DirectSRModule) {
        SetLastError("Failed to load DirectSR.dll. Make sure DirectSR.dll exists next to the executable or under the D3D12 folder.");
        return false;
    }

    auto getFunctionTable = reinterpret_cast<FNDSRExGetVersionedFunctionTable>(
        GetProcAddress(m_DirectSRModule, "DSRExGetVersionedFunctionTable"));
    if (!getFunctionTable) {
        SetLastError("DirectSR.dll does not export DSRExGetVersionedFunctionTable.");
        FreeLibrary(m_DirectSRModule);
        m_DirectSRModule = nullptr;
        return false;
    }

    HRESULT hr = getFunctionTable(DSR_EX_VERSION_1_0, &m_Table, sizeof(m_Table));
    if (FAILED(hr)) {
        SetLastError("DSRExGetVersionedFunctionTable failed with HRESULT " + HResultToString(hr) + ".");
        FreeLibrary(m_DirectSRModule);
        m_DirectSRModule = nullptr;
        return false;
    }

    if (!m_Table.pfnDSRExSuperResGetNumVariants
        || !m_Table.pfnDSRExSuperResEnumVariant
        || !m_Table.pfnDSRExSuperResQuerySourceSettings
        || !m_Table.pfnDSRExSuperResCreateEngine
        || !m_Table.pfnDSRExSuperResDestroyEngine
        || !m_Table.pfnDSRExSuperResCreateUpscaler
        || !m_Table.pfnDSRExSuperResDestroyUpscaler
        || !m_Table.pfnDSRExSuperResExecuteUpscaler) {
        SetLastError("DirectSR function table is incomplete.");
        FreeLibrary(m_DirectSRModule);
        m_DirectSRModule = nullptr;
        return false;
    }

    return true;
}

bool DirectSRUpscaler::SelectVariant(UINT preferredVariantIndex)
{
    const UINT variantCount = m_Table.pfnDSRExSuperResGetNumVariants(m_Device.Get());
    if (variantCount == 0) {
        SetLastError("DirectSR reported no available super resolution variants.");
        return false;
    }

    if (preferredVariantIndex != UINT_MAX) {
        if (preferredVariantIndex >= variantCount) {
            SetLastError("Preferred DirectSR variant index is out of range.");
            return false;
        }

        HRESULT hr = m_Table.pfnDSRExSuperResEnumVariant(preferredVariantIndex, m_Device.Get(), &m_SelectedVariantDesc);
        if (FAILED(hr)) {
            SetLastError("DirectSR EnumVariant failed with HRESULT " + HResultToString(hr) + ".");
            return false;
        }

        m_SelectedVariantIndex = preferredVariantIndex;
        return true;
    }

    UINT fallbackIndex = 0;
    DSR_SUPERRES_VARIANT_DESC fallbackDesc = {};
    bool hasFallback = false;

    for (UINT index = 0; index < variantCount; ++index) {
        DSR_SUPERRES_VARIANT_DESC desc = {};
        HRESULT hr = m_Table.pfnDSRExSuperResEnumVariant(index, m_Device.Get(), &desc);
        if (FAILED(hr)) {
            continue;
        }

        if (!hasFallback) {
            fallbackIndex = index;
            fallbackDesc = desc;
            hasFallback = true;
        }

        if ((desc.Flags & DSR_SUPERRES_VARIANT_FLAG_NATIVE) != 0) {
            m_SelectedVariantIndex = index;
            m_SelectedVariantDesc = desc;
            return true;
        }
    }

    if (!hasFallback) {
        SetLastError("Failed to enumerate DirectSR super resolution variants.");
        return false;
    }

    m_SelectedVariantIndex = fallbackIndex;
    m_SelectedVariantDesc = fallbackDesc;
    return true;
}

HRESULT DirectSRUpscaler::EnsureEngine(const EngineKey& key)
{
    if (m_HasEngineKey && m_EngineKey == key && m_Engine && m_Upscaler) {
        return S_OK;
    }

    DestroyEngine();

    HRESULT hr = QuerySourceSettings(key.TargetWidth, key.TargetHeight, key.TargetFormat, m_LastSourceSettings);
    if (FAILED(hr)) {
        SetLastError("DirectSR QuerySourceSettings failed with HRESULT " + HResultToString(hr) + ".");
        return hr;
    }

    DSR_SUPERRES_CREATE_ENGINE_PARAMETERS createParams = {};
    createParams.VariantId = m_SelectedVariantDesc.VariantId;
    createParams.TargetFormat = key.TargetFormat;
    createParams.SourceColorFormat = key.SourceColorFormat;
    createParams.SourceDepthFormat = key.SourceDepthFormat;
    createParams.ExposureScaleFormat = DXGI_FORMAT_UNKNOWN;
    createParams.Flags = m_CreateFlags;
    createParams.MaxSourceSize = { key.MaxSourceWidth, key.MaxSourceHeight };
    createParams.TargetSize = { key.TargetWidth, key.TargetHeight };

    hr = m_Table.pfnDSRExSuperResCreateEngine(
        m_SelectedVariantIndex,
        m_Device.Get(),
        &createParams,
        &m_Engine);
    if (FAILED(hr)) {
        SetLastError("DirectSR CreateEngine failed with HRESULT " + HResultToString(hr) + ".");
        return hr;
    }

    hr = m_Table.pfnDSRExSuperResCreateUpscaler(m_Engine, m_CommandQueue.Get(), &m_Upscaler);
    if (FAILED(hr)) {
        SetLastError("DirectSR CreateUpscaler failed with HRESULT " + HResultToString(hr) + ".");
        DestroyEngine();
        return hr;
    }

    m_EngineKey = key;
    m_HasEngineKey = true;
    return S_OK;
}

HRESULT DirectSRUpscaler::EnsureOutputTexture(UINT targetWidth, UINT targetHeight, DXGI_FORMAT targetFormat)
{
    if (m_OutputTexture && m_OutputWidth == targetWidth && m_OutputHeight == targetHeight && m_OutputFormat == targetFormat) {
        return S_OK;
    }

    DestroyOutputTexture();

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = targetWidth;
    desc.Height = targetHeight;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = targetFormat;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = m_Device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_OutputTexture));
    if (FAILED(hr)) {
        SetLastError("Failed to create DirectSR output texture with HRESULT " + HResultToString(hr) + ".");
        return hr;
    }

    m_OutputWidth = targetWidth;
    m_OutputHeight = targetHeight;
    m_OutputFormat = targetFormat;
    return S_OK;
}

void DirectSRUpscaler::DestroyEngine()
{
    if (m_Upscaler && m_Table.pfnDSRExSuperResDestroyUpscaler) {
        m_Table.pfnDSRExSuperResDestroyUpscaler(m_Upscaler);
    }
    m_Upscaler = nullptr;

    if (m_Engine && m_Table.pfnDSRExSuperResDestroyEngine) {
        m_Table.pfnDSRExSuperResDestroyEngine(m_Engine);
    }
    m_Engine = nullptr;
    m_HasEngineKey = false;
    m_EngineKey = {};
}

void DirectSRUpscaler::DestroyOutputTexture()
{
    m_OutputTexture.Reset();
    m_OutputWidth = 0;
    m_OutputHeight = 0;
    m_OutputFormat = DXGI_FORMAT_UNKNOWN;
}

void DirectSRUpscaler::ResetState()
{
    m_Device.Reset();
    m_CommandQueue.Reset();
    m_Table = {};
    m_SelectedVariantIndex = UINT_MAX;
    m_SelectedVariantDesc = {};
    m_LastSourceSettings = {};
    m_DefaultTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    m_DefaultSourceColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    m_DefaultSourceDepthFormat = DXGI_FORMAT_R32_FLOAT;
    m_OptimizationType = DSR_OPTIMIZATION_TYPE_BALANCED;
    m_CreateFlags = DSR_SUPERRES_CREATE_ENGINE_FLAG_NONE;
    m_EngineKey = {};
    m_HasEngineKey = false;
}

void DirectSRUpscaler::SetLastError(const std::string& message)
{
    m_LastError = message;
#if defined(_DEBUG)
    OutputDebugStringA((message + "\n").c_str());
#endif
}

D3D12_RECT DirectSRUpscaler::FullRect(UINT width, UINT height)
{
    return { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
}

D3D12_RECT DirectSRUpscaler::FullRect(ID3D12Resource* resource)
{
    UINT width = 0;
    UINT height = 0;
    GetTextureSize(resource, width, height);
    return FullRect(width, height);
}

bool DirectSRUpscaler::IsEmptyRect(const D3D12_RECT& rect)
{
    return rect.left == 0 && rect.top == 0 && rect.right == 0 && rect.bottom == 0;
}

bool DirectSRUpscaler::GetTextureSize(ID3D12Resource* resource, UINT& width, UINT& height)
{
    width = 0;
    height = 0;

    if (!resource) {
        return false;
    }

    const D3D12_RESOURCE_DESC desc = resource->GetDesc();
    if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
        return false;
    }

    width = static_cast<UINT>(desc.Width);
    height = desc.Height;
    return width > 0 && height > 0;
}

DXGI_FORMAT DirectSRUpscaler::TextureFormat(ID3D12Resource* resource)
{
    if (!resource) {
        return DXGI_FORMAT_UNKNOWN;
    }

    return resource->GetDesc().Format;
}

std::string DirectSRUpscaler::HResultToString(HRESULT hr)
{
    char buffer[16] = {};
    std::snprintf(buffer, sizeof(buffer), "0x%08X", static_cast<unsigned int>(hr));
    return buffer;
}

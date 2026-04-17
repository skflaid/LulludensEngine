#include "WinMLStyleTransferSystem.h"
#include "Renderer/RendererCore.h"
#include <Windows.h>
#include <DirectXPackedVector.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace
{
    void DebugLogA(const std::string& message)
    {
        OutputDebugStringA(message.c_str());
    }

    void DebugLogW(const std::wstring& message)
    {
        OutputDebugStringW(message.c_str());
    }

    float NormalizeByte(uint8_t value)
    {
        return static_cast<float>(value) / 255.0f;
    }

    uint8_t FloatToByte(float value)
    {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
    }

    uint32_t ClampCoord(uint32_t value, uint32_t maxExclusive)
    {
        return (maxExclusive == 0) ? 0u : (std::min)(value, maxExclusive - 1);
    }

    uint32_t SampleSourceCoord(uint32_t dstCoord, uint32_t dstExtent, uint32_t srcExtent)
    {
        if (dstExtent == 0 || srcExtent == 0) {
            return 0;
        }

        const float uv = (static_cast<float>(dstCoord) + 0.5f) / static_cast<float>(dstExtent);
        const uint32_t srcCoord = static_cast<uint32_t>(uv * static_cast<float>(srcExtent));
        return ClampCoord(srcCoord, srcExtent);
    }
}

WinMLStyleTransferSystem::WinMLStyleTransferSystem(RendererCore* rendererCore, Config config)
    : m_RendererCore(rendererCore), m_Config(std::move(config))
{
}

void WinMLStyleTransferSystem::Initialize()
{
    if (m_Config.modelPath.empty()) {
        m_Config.modelPath = GetDefaultModelPath();
    }

    DebugLogW(L"[WinML] Requested ONNX model: " + m_Config.modelPath + L"\n");

    m_RenderWidth = m_RendererCore->GetWidth();
    m_RenderHeight = m_RendererCore->GetHeight();

    if (m_Config.inputWidth == 0) {
        m_Config.inputWidth = m_RenderWidth;
    }

    if (m_Config.inputHeight == 0) {
        m_Config.inputHeight = m_RenderHeight;
    }

    CreateOutputTexture();
    CreateStagingBuffers();

    const size_t modelPixelCount = static_cast<size_t>(m_Config.inputWidth) * static_cast<size_t>(m_Config.inputHeight);
    const size_t renderPixelCount = static_cast<size_t>(m_RenderWidth) * static_cast<size_t>(m_RenderHeight);
    m_ImageTensor.resize(modelPixelCount * 3);
    m_DepthTensor.resize(modelPixelCount);
    m_NormalTensor.resize(modelPixelCount * 3);
    m_OutputPixels.resize(renderPixelCount * 4);

    m_BackendReady = LoadBackend();

    if (m_BackendReady) {
        DebugLogA("[WinML] Backend initialized successfully.\n");
    }
    else {
        DebugLogA("[WinML] Backend initialization failed. The renderer will fall back to the lighting buffer.\n");
    }
}

void WinMLStyleTransferSystem::Update(float)
{
}

void WinMLStyleTransferSystem::Shutdown()
{
    m_OutputTexture.Reset();
    m_LightingReadback.resource.Reset();
    m_DepthReadback.resource.Reset();
    m_NormalReadback.resource.Reset();
    m_OutputUpload.resource.Reset();
    m_BackendReady = false;
    m_ModelReady = false;
    m_OutputReady = false;

#if defined(LULLUDENS_HAS_WINML_STYLE)
    m_Binding = nullptr;
    m_Session = nullptr;
    m_Device = nullptr;
    m_Model = nullptr;
    m_InputNames.clear();
    m_OutputName.clear();
#endif
}

bool WinMLStyleTransferSystem::Execute()
{
    if (!IsReady()) {
        static bool s_LoggedNotReady = false;
        if (!s_LoggedNotReady) {
            DebugLogA("[WinML] Execute skipped because the backend or model is not ready.\n");
            s_LoggedNotReady = true;
        }
        return false;
    }

    return CaptureInputs() && RunInference() && UploadOutput();
}

void WinMLStyleTransferSystem::CreateOutputTexture()
{
    auto* device = m_RendererCore->GetDevice();

    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = m_RenderWidth;
    textureDesc.Height = m_RenderHeight;
    textureDesc.DepthOrArraySize = 1;
    textureDesc.MipLevels = 1;
    textureDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_OutputTexture)
    );
}

void WinMLStyleTransferSystem::CreateStagingBuffers()
{
    auto* device = m_RendererCore->GetDevice();

    auto createReadback = [&](ID3D12Resource* source, StagingBuffer& staging) {
        D3D12_RESOURCE_DESC srcDesc = source->GetDesc();
        device->GetCopyableFootprints(
            &srcDesc,
            0,
            1,
            0,
            &staging.footprint,
            &staging.numRows,
            &staging.rowSizeInBytes,
            &staging.totalBytes
        );

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = staging.totalBytes;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;

        device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&staging.resource)
        );
    };

    createReadback(m_RendererCore->GetLightingBuffer(), m_LightingReadback);
    createReadback(m_RendererCore->GetGBufferDepth(), m_DepthReadback);
    createReadback(m_RendererCore->GetGBufferNormal(), m_NormalReadback);

    D3D12_RESOURCE_DESC outputDesc = m_OutputTexture->GetDesc();
    device->GetCopyableFootprints(
        &outputDesc,
        0,
        1,
        0,
        &m_OutputUpload.footprint,
        &m_OutputUpload.numRows,
        &m_OutputUpload.rowSizeInBytes,
        &m_OutputUpload.totalBytes
    );

    D3D12_RESOURCE_DESC uploadDesc = {};
    uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadDesc.Width = m_OutputUpload.totalBytes;
    uploadDesc.Height = 1;
    uploadDesc.DepthOrArraySize = 1;
    uploadDesc.MipLevels = 1;
    uploadDesc.SampleDesc.Count = 1;
    uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES uploadHeapProps = {};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    device->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_OutputUpload.resource)
    );
}

bool WinMLStyleTransferSystem::LoadBackend()
{
#if defined(LULLUDENS_HAS_WINML_STYLE)
    if (!std::filesystem::exists(m_Config.modelPath)) {
        DebugLogW(L"[WinML] Model file not found: " + m_Config.modelPath + L"\n");
        return false;
    }

    try {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        }
        catch (const winrt::hresult_error& ex) {
            if (ex.code() != RPC_E_CHANGED_MODE) {
                throw;
            }
        }

        m_Model = winrt::Windows::AI::MachineLearning::LearningModel::LoadFromFilePath(m_Config.modelPath);
        m_Device = winrt::Windows::AI::MachineLearning::LearningModelDevice(
            winrt::Windows::AI::MachineLearning::LearningModelDeviceKind::DirectXHighPerformance);
        m_Session = winrt::Windows::AI::MachineLearning::LearningModelSession(m_Model, m_Device);
        m_Binding = winrt::Windows::AI::MachineLearning::LearningModelBinding(m_Session);

        std::ostringstream stream;
        stream << "[WinML] Session created. Inputs=" << m_Model.InputFeatures().Size()
               << ", Outputs=" << m_Model.OutputFeatures().Size() << "\n";

        m_InputNames.clear();
        for (const auto& feature : m_Model.InputFeatures()) {
            stream << "  input: " << winrt::to_string(feature.Name()) << "\n";
            m_InputNames.push_back(feature.Name());
        }

        if (m_Model.OutputFeatures().Size() > 0) {
            m_OutputName = m_Model.OutputFeatures().GetAt(0).Name();
        }

        for (const auto& feature : m_Model.OutputFeatures()) {
            stream << "  output: " << winrt::to_string(feature.Name()) << "\n";
        }

        DebugLogA(stream.str());

        if (m_InputNames.size() < 3) {
            DebugLogA("[WinML] The model must expose at least 3 inputs (image/depth/normal).\n");
            return false;
        }

        if (m_OutputName.empty()) {
            DebugLogA("[WinML] The model exposes no output tensors.\n");
            return false;
        }

        m_ModelReady = true;
        return true;
    }
    catch (const winrt::hresult_error& ex) {
        DebugLogA(std::string("[WinML] HRESULT exception: ") + winrt::to_string(ex.message()) + "\n");
        return false;
    }
    catch (const std::exception& ex) {
        DebugLogA(std::string("[WinML] Standard exception: ") + ex.what() + "\n");
        return false;
    }
#else
    DebugLogA(
        "[WinML] Backend is unavailable at compile time. "
        "Enable the Windows SDK cppwinrt headers and Windows.AI.MachineLearning support.\n");
    return false;
#endif
}

bool WinMLStyleTransferSystem::CaptureInputs()
{
    auto* commandList = m_RendererCore->GetCommandList();

    D3D12_RESOURCE_BARRIER toCopy[2] = {};
    toCopy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopy[0].Transition.pResource = m_RendererCore->GetGBufferDepth();
    toCopy[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toCopy[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toCopy[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    toCopy[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopy[1].Transition.pResource = m_RendererCore->GetGBufferNormal();
    toCopy[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toCopy[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toCopy[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList->ResourceBarrier(2, toCopy);

    auto copyTextureToBuffer = [&](ID3D12Resource* source, const StagingBuffer& dest) {
        D3D12_TEXTURE_COPY_LOCATION src = {};
        src.pResource = source;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION dst = {};
        dst.pResource = dest.resource.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = dest.footprint;

        commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    };

    copyTextureToBuffer(m_RendererCore->GetLightingBuffer(), m_LightingReadback);
    copyTextureToBuffer(m_RendererCore->GetGBufferDepth(), m_DepthReadback);
    copyTextureToBuffer(m_RendererCore->GetGBufferNormal(), m_NormalReadback);

    D3D12_RESOURCE_BARRIER toShader[2] = {};
    toShader[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toShader[0].Transition.pResource = m_RendererCore->GetGBufferDepth();
    toShader[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toShader[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toShader[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    toShader[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toShader[1].Transition.pResource = m_RendererCore->GetGBufferNormal();
    toShader[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toShader[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    toShader[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    commandList->ResourceBarrier(2, toShader);

    m_RendererCore->ExecuteCommandListAndWait();
    m_RendererCore->ResetCommandList();

    {
        uint8_t* mapped = nullptr;
        D3D12_RANGE range = { 0, static_cast<SIZE_T>(m_LightingReadback.totalBytes) };
        m_LightingReadback.resource->Map(0, &range, reinterpret_cast<void**>(&mapped));

        const size_t planeSize = static_cast<size_t>(m_Config.inputWidth) * m_Config.inputHeight;
        for (uint32_t y = 0; y < m_Config.inputHeight; ++y) {
            const uint32_t srcY = SampleSourceCoord(y, m_Config.inputHeight, m_RenderHeight);
            const uint8_t* row = mapped + static_cast<size_t>(srcY) * m_LightingReadback.footprint.Footprint.RowPitch;
            for (uint32_t x = 0; x < m_Config.inputWidth; ++x) {
                const uint32_t srcX = SampleSourceCoord(x, m_Config.inputWidth, m_RenderWidth);
                const uint8_t* pixel = row + srcX * 4;
                const size_t idx = static_cast<size_t>(y) * m_Config.inputWidth + x;
                m_ImageTensor[idx] = NormalizeByte(pixel[0]);
                m_ImageTensor[idx + planeSize] = NormalizeByte(pixel[1]);
                m_ImageTensor[idx + (planeSize * 2)] = NormalizeByte(pixel[2]);
            }
        }

        m_LightingReadback.resource->Unmap(0, nullptr);
    }

    {
        float* mapped = nullptr;
        D3D12_RANGE range = { 0, static_cast<SIZE_T>(m_DepthReadback.totalBytes) };
        m_DepthReadback.resource->Map(0, &range, reinterpret_cast<void**>(&mapped));

        float minDepth = FLT_MAX;
        float maxDepth = 0.0f;
        for (uint32_t y = 0; y < m_Config.inputHeight; ++y) {
            const uint32_t srcY = SampleSourceCoord(y, m_Config.inputHeight, m_RenderHeight);
            const uint8_t* rowBytes = reinterpret_cast<const uint8_t*>(mapped) + static_cast<size_t>(srcY) * m_DepthReadback.footprint.Footprint.RowPitch;
            const float* row = reinterpret_cast<const float*>(rowBytes);
            for (uint32_t x = 0; x < m_Config.inputWidth; ++x) {
                const uint32_t srcX = SampleSourceCoord(x, m_Config.inputWidth, m_RenderWidth);
                const size_t idx = static_cast<size_t>(y) * m_Config.inputWidth + x;
                m_DepthTensor[idx] = row[srcX];
                minDepth = (std::min)(minDepth, row[srcX]);
                maxDepth = (std::max)(maxDepth, row[srcX]);
            }
        }

        const float invRange = (maxDepth > minDepth) ? (1.0f / (maxDepth - minDepth)) : 1.0f;
        for (float& value : m_DepthTensor) {
            value = std::clamp((value - minDepth) * invRange, 0.0f, 1.0f);
        }

        m_DepthReadback.resource->Unmap(0, nullptr);
    }

    {
        uint16_t* mapped = nullptr;
        D3D12_RANGE range = { 0, static_cast<SIZE_T>(m_NormalReadback.totalBytes) };
        m_NormalReadback.resource->Map(0, &range, reinterpret_cast<void**>(&mapped));

        const size_t planeSize = static_cast<size_t>(m_Config.inputWidth) * m_Config.inputHeight;
        for (uint32_t y = 0; y < m_Config.inputHeight; ++y) {
            const uint32_t srcY = SampleSourceCoord(y, m_Config.inputHeight, m_RenderHeight);
            const uint8_t* rowBytes = reinterpret_cast<const uint8_t*>(mapped) + static_cast<size_t>(srcY) * m_NormalReadback.footprint.Footprint.RowPitch;
            const uint16_t* row = reinterpret_cast<const uint16_t*>(rowBytes);
            for (uint32_t x = 0; x < m_Config.inputWidth; ++x) {
                const uint32_t srcX = SampleSourceCoord(x, m_Config.inputWidth, m_RenderWidth);
                const size_t src = static_cast<size_t>(srcX) * 4;
                const size_t idx = static_cast<size_t>(y) * m_Config.inputWidth + x;
                m_NormalTensor[idx] = DirectX::PackedVector::XMConvertHalfToFloat(row[src + 0]);
                m_NormalTensor[idx + planeSize] = DirectX::PackedVector::XMConvertHalfToFloat(row[src + 1]);
                m_NormalTensor[idx + (planeSize * 2)] = DirectX::PackedVector::XMConvertHalfToFloat(row[src + 2]);
            }
        }

        m_NormalReadback.resource->Unmap(0, nullptr);
    }

    return true;
}

bool WinMLStyleTransferSystem::RunInference()
{
#if defined(LULLUDENS_HAS_WINML_STYLE)
    try {
        if (m_InputNames.size() < 3) {
            DebugLogA("[WinML] Expected 3 inputs (image/depth/normal).\n");
            return false;
        }

        std::vector<int64_t> imageShapeData = {
            1,
            3,
            static_cast<int64_t>(m_Config.inputHeight),
            static_cast<int64_t>(m_Config.inputWidth)
        };
        std::vector<int64_t> depthShapeData = {
            1,
            1,
            static_cast<int64_t>(m_Config.inputHeight),
            static_cast<int64_t>(m_Config.inputWidth)
        };

        auto imageShape = winrt::single_threaded_vector<int64_t>(std::move(imageShapeData));
        auto depthShape = winrt::single_threaded_vector<int64_t>(std::move(depthShapeData));

        auto imageTensor = winrt::Windows::AI::MachineLearning::TensorFloat::CreateFromArray(
            imageShape,
            winrt::array_view<const float>(m_ImageTensor));
        auto depthTensor = winrt::Windows::AI::MachineLearning::TensorFloat::CreateFromArray(
            depthShape,
            winrt::array_view<const float>(m_DepthTensor));
        auto normalTensor = winrt::Windows::AI::MachineLearning::TensorFloat::CreateFromArray(
            imageShape,
            winrt::array_view<const float>(m_NormalTensor));

        m_Binding.Clear();
        m_Binding.Bind(m_InputNames[0], imageTensor);
        m_Binding.Bind(m_InputNames[1], depthTensor);
        m_Binding.Bind(m_InputNames[2], normalTensor);

        auto result = m_Session.Evaluate(m_Binding, L"LulludensWinML");
        auto outputFeature = result.Outputs().Lookup(m_OutputName);
        auto outputTensor = outputFeature.as<winrt::Windows::AI::MachineLearning::TensorFloat>();
        auto outputView = outputTensor.GetAsVectorView();

        const size_t planeSize = static_cast<size_t>(m_Config.inputWidth) * m_Config.inputHeight;
        const uint32_t expectedValues = static_cast<uint32_t>(planeSize * 3);
        if (outputView.Size() < expectedValues) {
            std::ostringstream stream;
            stream << "[WinML] Output tensor is smaller than expected. size=" << outputView.Size()
                   << ", expected=" << expectedValues << "\n";
            DebugLogA(stream.str());
            return false;
        }

        for (uint32_t y = 0; y < m_RenderHeight; ++y) {
            const uint32_t modelY = SampleSourceCoord(y, m_RenderHeight, m_Config.inputHeight);
            for (uint32_t x = 0; x < m_RenderWidth; ++x) {
                const uint32_t modelX = SampleSourceCoord(x, m_RenderWidth, m_Config.inputWidth);
                const size_t modelIdx = static_cast<size_t>(modelY) * m_Config.inputWidth + modelX;
                const size_t dst = (static_cast<size_t>(y) * m_RenderWidth + x) * 4;
                m_OutputPixels[dst + 0] = FloatToByte(outputView.GetAt(static_cast<uint32_t>(modelIdx)));
                m_OutputPixels[dst + 1] = FloatToByte(outputView.GetAt(static_cast<uint32_t>(modelIdx + planeSize)));
                m_OutputPixels[dst + 2] = FloatToByte(outputView.GetAt(static_cast<uint32_t>(modelIdx + planeSize * 2)));
                m_OutputPixels[dst + 3] = 255;
            }
        }

        return true;
    }
    catch (const winrt::hresult_error& ex) {
        DebugLogA(std::string("[WinML] Inference HRESULT exception: ") + winrt::to_string(ex.message()) + "\n");
        return false;
    }
#else
    return false;
#endif
}

bool WinMLStyleTransferSystem::UploadOutput()
{
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    m_OutputUpload.resource->Map(0, &readRange, reinterpret_cast<void**>(&mapped));

    for (uint32_t y = 0; y < m_RenderHeight; ++y) {
        uint8_t* row = mapped + static_cast<size_t>(y) * m_OutputUpload.footprint.Footprint.RowPitch;
        const uint8_t* src = m_OutputPixels.data() + static_cast<size_t>(y) * m_RenderWidth * 4;
        memcpy(row, src, static_cast<size_t>(m_RenderWidth) * 4);
    }

    m_OutputUpload.resource->Unmap(0, nullptr);

    auto* commandList = m_RendererCore->GetCommandList();

    if (m_OutputReady) {
        D3D12_RESOURCE_BARRIER toCopyDest = {};
        toCopyDest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopyDest.Transition.pResource = m_OutputTexture.Get();
        toCopyDest.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        toCopyDest.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        toCopyDest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &toCopyDest);
    }

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = m_OutputTexture.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = m_OutputUpload.resource.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = m_OutputUpload.footprint;

    commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER toCopySource = {};
    toCopySource.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    toCopySource.Transition.pResource = m_OutputTexture.Get();
    toCopySource.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    toCopySource.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    toCopySource.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &toCopySource);

    m_OutputReady = true;
    return true;
}

std::wstring WinMLStyleTransferSystem::GetDefaultModelPath() const
{
    return L"C:\\LocalRepository\\CapstoneDesign\\Learning\\net4\\net4.onnx";
}

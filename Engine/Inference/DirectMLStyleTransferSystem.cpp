#include "DirectMLStyleTransferSystem.h"
#include "Renderer/RendererCore.h"
#include <Windows.h>
#include <DirectXPackedVector.h>
#include <algorithm>
#include <array>
#include <cfloat>
#include <filesystem>

using Microsoft::WRL::ComPtr;

namespace
{
    // 8bit UNORM 색상 채널을 0~1 float로 정규화한다.
    float NormalizeByte(uint8_t value)
    {
        return static_cast<float>(value) / 255.0f;
    }

    // 0~1 float 채널 값을 8bit UNORM으로 변환한다.
    uint8_t FloatToByte(float value)
    {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
    }
}

// 렌더러와 설정을 받아 스타일 트랜스퍼 시스템을 구성한다.
DirectMLStyleTransferSystem::DirectMLStyleTransferSystem(RendererCore* rendererCore, Config config)
    : m_RendererCore(rendererCore), m_Config(std::move(config))
{
}

// 해상도에 맞는 출력 텍스처/스테이징 버퍼를 만들고 DirectML 백엔드를 연다.
void DirectMLStyleTransferSystem::Initialize()
{
    if (m_Config.modelPath.empty()) {
        m_Config.modelPath = GetDefaultModelPath();
    }

    m_Config.inputWidth = m_RendererCore->GetWidth();
    m_Config.inputHeight = m_RendererCore->GetHeight();

    CreateOutputTexture();
    CreateStagingBuffers();

    const size_t pixelCount = static_cast<size_t>(m_Config.inputWidth) * static_cast<size_t>(m_Config.inputHeight);
    m_ImageTensor.resize(pixelCount * 3);
    m_DepthTensor.resize(pixelCount);
    m_NormalTensor.resize(pixelCount * 3);
    m_OutputPixels.resize(pixelCount * 4);

    m_BackendReady = LoadBackend();
}

// 현재 구현에서는 매 프레임 지속 상태를 따로 계산하지 않는다.
void DirectMLStyleTransferSystem::Update(float)
{
}

// 추론 세션과 임시 버퍼를 모두 해제한다.
void DirectMLStyleTransferSystem::Shutdown()
{
    m_OutputTexture.Reset();
    m_LightingReadback.resource.Reset();
    m_DepthReadback.resource.Reset();
    m_NormalReadback.resource.Reset();
    m_OutputUpload.resource.Reset();
    m_BackendReady = false;
    m_ModelReady = false;
    m_OutputReady = false;

#if defined(LULLUDENS_HAS_DIRECTML_STYLE)
    m_OrtSession.reset();
    m_OrtEnv.reset();
    m_InputNames.clear();
    m_InputNameViews.clear();
    m_OutputNames.clear();
    m_OutputNameViews.clear();
#endif
}

// 한 프레임의 스타일 추론 전체 파이프라인을 실행한다.
bool DirectMLStyleTransferSystem::Execute()
{
    if (!IsReady()) {
        return false;
    }

    return CaptureInputs() && RunInference() && UploadOutput();
}

// 추론 결과를 담을 GPU 텍스처를 생성한다.
void DirectMLStyleTransferSystem::CreateOutputTexture()
{
    auto* device = m_RendererCore->GetDevice();

    D3D12_RESOURCE_DESC textureDesc = {};
    textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    textureDesc.Width = m_Config.inputWidth;
    textureDesc.Height = m_Config.inputHeight;
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

// GPU 입력 텍스처를 CPU로 읽을 readback 버퍼와 결과 upload 버퍼를 만든다.
void DirectMLStyleTransferSystem::CreateStagingBuffers()
{
    auto* device = m_RendererCore->GetDevice();

    // source 텍스처 형식에 맞는 readback 버퍼 하나를 준비한다.
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

// DirectML 실행 공급자를 연결한 ONNX Runtime 세션을 로드한다.
bool DirectMLStyleTransferSystem::LoadBackend()
{
#if defined(LULLUDENS_HAS_DIRECTML_STYLE)
    if (!std::filesystem::exists(m_Config.modelPath)) {
        OutputDebugStringW((L"DirectML style model not found: " + m_Config.modelPath + L"\n").c_str());
        return false;
    }

    try {
        m_OrtEnv = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "LulludensDirectMLStyle");
        Ort::SessionOptions sessionOptions;
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

        OrtStatus* status = OrtSessionOptionsAppendExecutionProvider_DML(sessionOptions, 0);
        if (status != nullptr) {
            Ort::GetApi().ReleaseStatus(status);
            return false;
        }

        m_OrtSession = std::make_unique<Ort::Session>(*m_OrtEnv, m_Config.modelPath.c_str(), sessionOptions);

        Ort::AllocatorWithDefaultOptions allocator;
        const size_t inputCount = m_OrtSession->GetInputCount();
        const size_t outputCount = m_OrtSession->GetOutputCount();

        for (size_t i = 0; i < inputCount; ++i) {
            auto name = m_OrtSession->GetInputNameAllocated(i, allocator);
            m_InputNames.emplace_back(name.get());
            m_InputNameViews.push_back(m_InputNames.back().c_str());
        }

        for (size_t i = 0; i < outputCount; ++i) {
            auto name = m_OrtSession->GetOutputNameAllocated(i, allocator);
            m_OutputNames.emplace_back(name.get());
            m_OutputNameViews.push_back(m_OutputNames.back().c_str());
        }

        m_ModelReady = !m_InputNameViews.empty() && !m_OutputNameViews.empty();
        return m_ModelReady;
    }
    catch (...) {
        return false;
    }
#else
    OutputDebugStringA("DirectML backend is disabled. Define LULLUDENS_ENABLE_DIRECTML with ONNX Runtime DirectML installed.\n");
    return false;
#endif
}

// Lighting/Depth/Normal GPU 텍스처를 CPU 메모리로 복사하고 모델 입력 텐서 형식으로 정리한다.
bool DirectMLStyleTransferSystem::CaptureInputs()
{
    auto* commandList = m_RendererCore->GetCommandList();

    // Depth/Normal은 직전 패스에서 SRV 상태이므로 copy source로 전환한다.
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

    // GPU 텍스처 한 장을 선형 readback 버퍼로 복사한다.
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
        // Lighting 결과를 RGB NCHW 텐서로 펼친다.
        uint8_t* mapped = nullptr;
        D3D12_RANGE range = { 0, static_cast<SIZE_T>(m_LightingReadback.totalBytes) };
        m_LightingReadback.resource->Map(0, &range, reinterpret_cast<void**>(&mapped));

        const size_t planeSize = static_cast<size_t>(m_Config.inputWidth) * m_Config.inputHeight;
        for (uint32_t y = 0; y < m_Config.inputHeight; ++y) {
            const uint8_t* row = mapped + static_cast<size_t>(y) * m_LightingReadback.footprint.Footprint.RowPitch;
            for (uint32_t x = 0; x < m_Config.inputWidth; ++x) {
                const uint8_t* pixel = row + x * 4;
                const size_t idx = static_cast<size_t>(y) * m_Config.inputWidth + x;
                m_ImageTensor[idx] = NormalizeByte(pixel[0]);
                m_ImageTensor[idx + planeSize] = NormalizeByte(pixel[1]);
                m_ImageTensor[idx + (planeSize * 2)] = NormalizeByte(pixel[2]);
            }
        }

        m_LightingReadback.resource->Unmap(0, nullptr);
    }

    {
        // Depth 결과를 0~1 범위로 정규화해 단일 채널 텐서로 만든다.
        float* mapped = nullptr;
        D3D12_RANGE range = { 0, static_cast<SIZE_T>(m_DepthReadback.totalBytes) };
        m_DepthReadback.resource->Map(0, &range, reinterpret_cast<void**>(&mapped));

        float minDepth = FLT_MAX;
        float maxDepth = 0.0f;
        for (uint32_t y = 0; y < m_Config.inputHeight; ++y) {
            const uint8_t* rowBytes = reinterpret_cast<const uint8_t*>(mapped) + static_cast<size_t>(y) * m_DepthReadback.footprint.Footprint.RowPitch;
            const float* row = reinterpret_cast<const float*>(rowBytes);
            for (uint32_t x = 0; x < m_Config.inputWidth; ++x) {
                const size_t idx = static_cast<size_t>(y) * m_Config.inputWidth + x;
                m_DepthTensor[idx] = row[x];
                minDepth = (std::min)(minDepth, row[x]);
                maxDepth = (std::max)(maxDepth, row[x]);
            }
        }

        const float invRange = (maxDepth > minDepth) ? (1.0f / (maxDepth - minDepth)) : 1.0f;
        for (float& value : m_DepthTensor) {
            value = std::clamp((value - minDepth) * invRange, 0.0f, 1.0f);
        }

        m_DepthReadback.resource->Unmap(0, nullptr);
    }

    {
        // Half float normal 버퍼를 float 텐서로 변환한다.
        uint16_t* mapped = nullptr;
        D3D12_RANGE range = { 0, static_cast<SIZE_T>(m_NormalReadback.totalBytes) };
        m_NormalReadback.resource->Map(0, &range, reinterpret_cast<void**>(&mapped));

        const size_t planeSize = static_cast<size_t>(m_Config.inputWidth) * m_Config.inputHeight;
        for (uint32_t y = 0; y < m_Config.inputHeight; ++y) {
            const uint8_t* rowBytes = reinterpret_cast<const uint8_t*>(mapped) + static_cast<size_t>(y) * m_NormalReadback.footprint.Footprint.RowPitch;
            const uint16_t* row = reinterpret_cast<const uint16_t*>(rowBytes);
            for (uint32_t x = 0; x < m_Config.inputWidth; ++x) {
                const size_t src = static_cast<size_t>(x) * 4;
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

// 준비된 텐서를 ONNX Runtime DirectML 세션에 전달하고 스타일 결과를 받는다.
bool DirectMLStyleTransferSystem::RunInference()
{
#if defined(LULLUDENS_HAS_DIRECTML_STYLE)
    const std::array<int64_t, 4> imageShape = { 1, 3, static_cast<int64_t>(m_Config.inputHeight), static_cast<int64_t>(m_Config.inputWidth) };
    const std::array<int64_t, 4> depthShape = { 1, 1, static_cast<int64_t>(m_Config.inputHeight), static_cast<int64_t>(m_Config.inputWidth) };

    Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    Ort::Value imageTensor = Ort::Value::CreateTensor<float>(memoryInfo, m_ImageTensor.data(), m_ImageTensor.size(), imageShape.data(), imageShape.size());
    Ort::Value depthTensor = Ort::Value::CreateTensor<float>(memoryInfo, m_DepthTensor.data(), m_DepthTensor.size(), depthShape.data(), depthShape.size());
    Ort::Value normalTensor = Ort::Value::CreateTensor<float>(memoryInfo, m_NormalTensor.data(), m_NormalTensor.size(), imageShape.data(), imageShape.size());

    std::array<Ort::Value, 3> inputs = { std::move(imageTensor), std::move(depthTensor), std::move(normalTensor) };
    auto outputs = m_OrtSession->Run(
        Ort::RunOptions{ nullptr },
        m_InputNameViews.data(),
        inputs.data(),
        inputs.size(),
        m_OutputNameViews.data(),
        1
    );

    if (outputs.empty() || !outputs[0].IsTensor()) {
        return false;
    }

    const float* output = outputs[0].GetTensorData<float>();
    const size_t planeSize = static_cast<size_t>(m_Config.inputWidth) * m_Config.inputHeight;
    for (uint32_t y = 0; y < m_Config.inputHeight; ++y) {
        for (uint32_t x = 0; x < m_Config.inputWidth; ++x) {
            const size_t idx = static_cast<size_t>(y) * m_Config.inputWidth + x;
            const size_t dst = idx * 4;
            m_OutputPixels[dst + 0] = FloatToByte(output[idx]);
            m_OutputPixels[dst + 1] = FloatToByte(output[idx + planeSize]);
            m_OutputPixels[dst + 2] = FloatToByte(output[idx + planeSize * 2]);
            m_OutputPixels[dst + 3] = 255;
        }
    }

    return true;
#else
    return false;
#endif
}

// 추론 결과 바이트 배열을 GPU 텍스처로 업로드해 후속 복사에 쓸 수 있게 만든다.
bool DirectMLStyleTransferSystem::UploadOutput()
{
    uint8_t* mapped = nullptr;
    D3D12_RANGE readRange = { 0, 0 };
    m_OutputUpload.resource->Map(0, &readRange, reinterpret_cast<void**>(&mapped));

    for (uint32_t y = 0; y < m_Config.inputHeight; ++y) {
        uint8_t* row = mapped + static_cast<size_t>(y) * m_OutputUpload.footprint.Footprint.RowPitch;
        const uint8_t* src = m_OutputPixels.data() + static_cast<size_t>(y) * m_Config.inputWidth * 4;
        memcpy(row, src, static_cast<size_t>(m_Config.inputWidth) * 4);
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

// 기본 모델 위치는 CapstoneDesign 쪽 ONNX export 산출물로 가정한다.
std::wstring DirectMLStyleTransferSystem::GetDefaultModelPath() const
{
    return L"C:\\LocalRepository\\CapstoneDesign\\Learning\\net4\\reconet_style.onnx";
}

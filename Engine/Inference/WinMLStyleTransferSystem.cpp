#include "WinMLStyleTransferSystem.h"
#include "Renderer/RendererCore.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <sstream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr uint32_t kTensorizeDescriptorCount = 7;
    constexpr uint32_t kDetensorizeDescriptorCount = 2;
    constexpr uint32_t kThreadsX = 8;
    constexpr uint32_t kThreadsY = 8;

    void DebugLogA(const std::string& message)
    {
        OutputDebugStringA(message.c_str());
    }

    void DebugLogW(const std::wstring& message)
    {
        OutputDebugStringW(message.c_str());
    }

    D3D12_RESOURCE_DESC CreateStructuredBufferDesc(UINT64 byteSize, bool allowUnorderedAccess)
    {
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = byteSize;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = allowUnorderedAccess ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
        return desc;
    }

    void CreateStructuredBuffer(
        ID3D12Device* device,
        uint32_t elementCount,
        uint32_t elementStride,
        bool allowUnorderedAccess,
        D3D12_RESOURCE_STATES initialState,
        WinMLStyleTransferSystem::GpuBuffer& buffer)
    {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        const UINT64 byteSize = static_cast<UINT64>(elementCount) * elementStride;
        const D3D12_RESOURCE_DESC desc = CreateStructuredBufferDesc(byteSize, allowUnorderedAccess);

        ThrowIfFailed(device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            initialState,
            nullptr,
            IID_PPV_ARGS(&buffer.resource)));

        buffer.elementCount = elementCount;
        buffer.elementStride = elementStride;
    }

    void CreateBufferSrv(
        ID3D12Device* device,
        ID3D12Resource* resource,
        uint32_t elementCount,
        uint32_t elementStride,
        D3D12_CPU_DESCRIPTOR_HANDLE handle)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc = {};
        desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.Buffer.FirstElement = 0;
        desc.Buffer.NumElements = elementCount;
        desc.Buffer.StructureByteStride = elementStride;
        desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        device->CreateShaderResourceView(resource, &desc, handle);
    }

    void CreateBufferUav(
        ID3D12Device* device,
        ID3D12Resource* resource,
        uint32_t elementCount,
        uint32_t elementStride,
        D3D12_CPU_DESCRIPTOR_HANDLE handle)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {};
        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.Buffer.FirstElement = 0;
        desc.Buffer.NumElements = elementCount;
        desc.Buffer.StructureByteStride = elementStride;
        desc.Buffer.CounterOffsetInBytes = 0;
        desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        device->CreateUnorderedAccessView(resource, nullptr, &desc, handle);
    }

    void TransitionResource(
        ID3D12GraphicsCommandList* commandList,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after)
    {
        if (before == after) {
            return;
        }

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);
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
    CreateTensorResources();
    CreateDescriptorHeaps();
    CreateComputePipeline();

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
    m_ImageTensorBuffer.resource.Reset();
    m_DepthTensorBuffer.resource.Reset();
    m_NormalTensorBuffer.resource.Reset();
    m_OutputTensorBuffer.resource.Reset();
    m_DepthMinMaxBuffer.resource.Reset();
    m_TensorizeHeap.Reset();
    m_DetensorizeHeap.Reset();
    m_TensorizeRootSignature.Reset();
    m_DetensorizeRootSignature.Reset();
    m_ResetDepthMinMaxPSO.Reset();
    m_TensorizePSO.Reset();
    m_NormalizeDepthPSO.Reset();
    m_DetensorizePSO.Reset();
    m_BackendReady = false;
    m_ModelReady = false;
    m_OutputReady = false;
    m_InputBuffersNeedUavTransition = false;

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
    textureDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &textureDesc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_OutputTexture)));
}

void WinMLStyleTransferSystem::CreateTensorResources()
{
    const uint32_t modelPixelCount = m_Config.inputWidth * m_Config.inputHeight;
    auto* device = m_RendererCore->GetDevice();

    CreateStructuredBuffer(
        device,
        modelPixelCount * 3,
        sizeof(float),
        true,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        m_ImageTensorBuffer);
    CreateStructuredBuffer(
        device,
        modelPixelCount,
        sizeof(float),
        true,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        m_DepthTensorBuffer);
    CreateStructuredBuffer(
        device,
        modelPixelCount * 3,
        sizeof(float),
        true,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        m_NormalTensorBuffer);
    CreateStructuredBuffer(
        device,
        modelPixelCount * 3,
        sizeof(float),
        true,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        m_OutputTensorBuffer);
    CreateStructuredBuffer(
        device,
        2,
        sizeof(uint32_t),
        true,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        m_DepthMinMaxBuffer);

    m_InputBuffersNeedUavTransition = false;
}

void WinMLStyleTransferSystem::CreateDescriptorHeaps()
{
    auto* device = m_RendererCore->GetDevice();
    const UINT descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC tensorizeHeapDesc = {};
    tensorizeHeapDesc.NumDescriptors = kTensorizeDescriptorCount;
    tensorizeHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    tensorizeHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&tensorizeHeapDesc, IID_PPV_ARGS(&m_TensorizeHeap)));

    D3D12_CPU_DESCRIPTOR_HANDLE tensorizeHandle = m_TensorizeHeap->GetCPUDescriptorHandleForHeapStart();
    device->CreateShaderResourceView(m_RendererCore->GetLightingBuffer(), nullptr, tensorizeHandle);
    tensorizeHandle.ptr += descriptorSize;
    device->CreateShaderResourceView(m_RendererCore->GetGBufferDepth(), nullptr, tensorizeHandle);
    tensorizeHandle.ptr += descriptorSize;
    device->CreateShaderResourceView(m_RendererCore->GetGBufferNormal(), nullptr, tensorizeHandle);
    tensorizeHandle.ptr += descriptorSize;
    CreateBufferUav(device, m_ImageTensorBuffer.resource.Get(), m_ImageTensorBuffer.elementCount, m_ImageTensorBuffer.elementStride, tensorizeHandle);
    tensorizeHandle.ptr += descriptorSize;
    CreateBufferUav(device, m_DepthTensorBuffer.resource.Get(), m_DepthTensorBuffer.elementCount, m_DepthTensorBuffer.elementStride, tensorizeHandle);
    tensorizeHandle.ptr += descriptorSize;
    CreateBufferUav(device, m_NormalTensorBuffer.resource.Get(), m_NormalTensorBuffer.elementCount, m_NormalTensorBuffer.elementStride, tensorizeHandle);
    tensorizeHandle.ptr += descriptorSize;
    CreateBufferUav(device, m_DepthMinMaxBuffer.resource.Get(), m_DepthMinMaxBuffer.elementCount, m_DepthMinMaxBuffer.elementStride, tensorizeHandle);

    D3D12_DESCRIPTOR_HEAP_DESC detensorizeHeapDesc = {};
    detensorizeHeapDesc.NumDescriptors = kDetensorizeDescriptorCount;
    detensorizeHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    detensorizeHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ThrowIfFailed(device->CreateDescriptorHeap(&detensorizeHeapDesc, IID_PPV_ARGS(&m_DetensorizeHeap)));

    D3D12_CPU_DESCRIPTOR_HANDLE detensorizeHandle = m_DetensorizeHeap->GetCPUDescriptorHandleForHeapStart();
    CreateBufferSrv(device, m_OutputTensorBuffer.resource.Get(), m_OutputTensorBuffer.elementCount, m_OutputTensorBuffer.elementStride, detensorizeHandle);
    detensorizeHandle.ptr += descriptorSize;

    D3D12_UNORDERED_ACCESS_VIEW_DESC outputUavDesc = {};
    outputUavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    outputUavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    outputUavDesc.Texture2D.MipSlice = 0;
    outputUavDesc.Texture2D.PlaneSlice = 0;
    device->CreateUnorderedAccessView(m_OutputTexture.Get(), nullptr, &outputUavDesc, detensorizeHandle);
}

void WinMLStyleTransferSystem::CreateComputePipeline()
{
    auto* device = m_RendererCore->GetDevice();
    const std::wstring shaderPath = L"Renderer/Shaders/StyleTransferTensor.hlsl";

    {
        D3D12_DESCRIPTOR_RANGE srvRanges[3] = {};
        for (UINT i = 0; i < 3; ++i) {
            srvRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            srvRanges[i].NumDescriptors = 1;
            srvRanges[i].BaseShaderRegister = i;
            srvRanges[i].RegisterSpace = 0;
            srvRanges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        }

        D3D12_DESCRIPTOR_RANGE uavRanges[4] = {};
        for (UINT i = 0; i < 4; ++i) {
            uavRanges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
            uavRanges[i].NumDescriptors = 1;
            uavRanges[i].BaseShaderRegister = i;
            uavRanges[i].RegisterSpace = 0;
            uavRanges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
        }

        D3D12_ROOT_PARAMETER rootParameters[3] = {};
        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameters[0].Constants.ShaderRegister = 0;
        rootParameters[0].Constants.RegisterSpace = 0;
        rootParameters[0].Constants.Num32BitValues = 4;
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(srvRanges);
        rootParameters[1].DescriptorTable.pDescriptorRanges = srvRanges;
        rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(uavRanges);
        rootParameters[2].DescriptorTable.pDescriptorRanges = uavRanges;
        rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.NumParameters = _countof(rootParameters);
        rootDesc.pParameters = rootParameters;
        rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> sig;
        ComPtr<ID3DBlob> err;
        ThrowIfFailed(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
        ThrowIfFailed(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_TensorizeRootSignature)));

        auto resetCs = d3dUtil::CompileShader(shaderPath, nullptr, "ResetDepthMinMaxCS", "cs_5_0");
        auto tensorizeCs = d3dUtil::CompileShader(shaderPath, nullptr, "TensorizeCS", "cs_5_0");
        auto normalizeCs = d3dUtil::CompileShader(shaderPath, nullptr, "NormalizeDepthCS", "cs_5_0");

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_TensorizeRootSignature.Get();

        psoDesc.CS = { resetCs->GetBufferPointer(), resetCs->GetBufferSize() };
        ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_ResetDepthMinMaxPSO)));

        psoDesc.CS = { tensorizeCs->GetBufferPointer(), tensorizeCs->GetBufferSize() };
        ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_TensorizePSO)));

        psoDesc.CS = { normalizeCs->GetBufferPointer(), normalizeCs->GetBufferSize() };
        ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_NormalizeDepthPSO)));
    }

    {
        D3D12_DESCRIPTOR_RANGE srvRange = {};
        srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        srvRange.NumDescriptors = 1;
        srvRange.BaseShaderRegister = 3;
        srvRange.RegisterSpace = 0;
        srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_DESCRIPTOR_RANGE uavRange = {};
        uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        uavRange.NumDescriptors = 1;
        uavRange.BaseShaderRegister = 4;
        uavRange.RegisterSpace = 0;
        uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

        D3D12_ROOT_PARAMETER rootParameters[3] = {};
        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        rootParameters[0].Constants.ShaderRegister = 0;
        rootParameters[0].Constants.RegisterSpace = 0;
        rootParameters[0].Constants.Num32BitValues = 4;
        rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[1].DescriptorTable.pDescriptorRanges = &srvRange;
        rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;
        rootParameters[2].DescriptorTable.pDescriptorRanges = &uavRange;
        rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
        rootDesc.NumParameters = _countof(rootParameters);
        rootDesc.pParameters = rootParameters;
        rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> sig;
        ComPtr<ID3DBlob> err;
        ThrowIfFailed(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err));
        ThrowIfFailed(device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(), IID_PPV_ARGS(&m_DetensorizeRootSignature)));

        auto detensorizeCs = d3dUtil::CompileShader(shaderPath, nullptr, "DetensorizeCS", "cs_5_0");

        D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = m_DetensorizeRootSignature.Get();
        psoDesc.CS = { detensorizeCs->GetBufferPointer(), detensorizeCs->GetBufferSize() };
        ThrowIfFailed(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&m_DetensorizePSO)));
    }
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
            if (auto tensorFeature = feature.try_as<winrt::Windows::AI::MachineLearning::TensorFeatureDescriptor>()) {
                stream << "    shape:";
                for (const int64_t dim : tensorFeature.Shape()) {
                    stream << " " << dim;
                }
                stream << "\n";
            }
            m_InputNames.push_back(feature.Name());
        }

        if (m_Model.OutputFeatures().Size() > 0) {
            m_OutputName = m_Model.OutputFeatures().GetAt(0).Name();
        }

        for (const auto& feature : m_Model.OutputFeatures()) {
            stream << "  output: " << winrt::to_string(feature.Name()) << "\n";
            if (auto tensorFeature = feature.try_as<winrt::Windows::AI::MachineLearning::TensorFeatureDescriptor>()) {
                stream << "    shape:";
                for (const int64_t dim : tensorFeature.Shape()) {
                    stream << " " << dim;
                }
                stream << "\n";
            }
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

void WinMLStyleTransferSystem::DispatchTensorization()
{
    auto* commandList = m_RendererCore->GetCommandList();
    const std::array<uint32_t, 4> constants = {
        m_RenderWidth,
        m_RenderHeight,
        m_Config.inputWidth,
        m_Config.inputHeight
    };

    ID3D12DescriptorHeap* heaps[] = { m_TensorizeHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetComputeRootSignature(m_TensorizeRootSignature.Get());
    commandList->SetComputeRoot32BitConstants(0, static_cast<UINT>(constants.size()), constants.data(), 0);

    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = m_TensorizeHeap->GetGPUDescriptorHandleForHeapStart();
    commandList->SetComputeRootDescriptorTable(1, srvHandle);

    D3D12_GPU_DESCRIPTOR_HANDLE uavHandle = srvHandle;
    const UINT descriptorSize = m_RendererCore->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    uavHandle.ptr += static_cast<UINT64>(3) * descriptorSize;
    commandList->SetComputeRootDescriptorTable(2, uavHandle);

    commandList->SetPipelineState(m_ResetDepthMinMaxPSO.Get());
    commandList->Dispatch(1, 1, 1);

    commandList->SetPipelineState(m_TensorizePSO.Get());
    commandList->Dispatch((m_Config.inputWidth + (kThreadsX - 1)) / kThreadsX, (m_Config.inputHeight + (kThreadsY - 1)) / kThreadsY, 1);

    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_DepthMinMaxBuffer.resource.Get();
    commandList->ResourceBarrier(1, &uavBarrier);

    commandList->SetPipelineState(m_NormalizeDepthPSO.Get());
    commandList->Dispatch((m_Config.inputWidth + (kThreadsX - 1)) / kThreadsX, (m_Config.inputHeight + (kThreadsY - 1)) / kThreadsY, 1);
}

bool WinMLStyleTransferSystem::CaptureInputs()
{
    auto* commandList = m_RendererCore->GetCommandList();

    TransitionResource(commandList, m_RendererCore->GetLightingBuffer(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    TransitionResource(commandList, m_RendererCore->GetGBufferDepth(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    TransitionResource(commandList, m_RendererCore->GetGBufferNormal(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    if (m_InputBuffersNeedUavTransition) {
        TransitionResource(commandList, m_ImageTensorBuffer.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        TransitionResource(commandList, m_DepthTensorBuffer.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        TransitionResource(commandList, m_NormalTensorBuffer.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        TransitionResource(commandList, m_DepthMinMaxBuffer.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    DispatchTensorization();

    D3D12_RESOURCE_BARRIER uavBarriers[4] = {};
    ID3D12Resource* uavResources[4] = {
        m_ImageTensorBuffer.resource.Get(),
        m_DepthTensorBuffer.resource.Get(),
        m_NormalTensorBuffer.resource.Get(),
        m_DepthMinMaxBuffer.resource.Get()
    };
    for (UINT i = 0; i < _countof(uavResources); ++i) {
        uavBarriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarriers[i].UAV.pResource = uavResources[i];
    }
    commandList->ResourceBarrier(_countof(uavBarriers), uavBarriers);

    TransitionResource(commandList, m_ImageTensorBuffer.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    TransitionResource(commandList, m_DepthTensorBuffer.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    TransitionResource(commandList, m_NormalTensorBuffer.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    TransitionResource(commandList, m_DepthMinMaxBuffer.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
    m_InputBuffersNeedUavTransition = true;

    TransitionResource(commandList, m_RendererCore->GetLightingBuffer(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE);
    TransitionResource(commandList, m_RendererCore->GetGBufferDepth(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TransitionResource(commandList, m_RendererCore->GetGBufferNormal(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    m_RendererCore->ExecuteCommandListAndWait();
    m_RendererCore->ResetCommandList();
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

        winrt::com_ptr<ITensorStaticsNative> tensorFactory =
            winrt::get_activation_factory<winrt::Windows::AI::MachineLearning::TensorFloat, ITensorStaticsNative>();

        const int64_t imageShape[] = {
            1,
            3,
            static_cast<int64_t>(m_Config.inputHeight),
            static_cast<int64_t>(m_Config.inputWidth)
        };
        const int64_t depthShape[] = {
            1,
            1,
            static_cast<int64_t>(m_Config.inputHeight),
            static_cast<int64_t>(m_Config.inputWidth)
        };

        auto createTensorFromResource =
            [&](ID3D12Resource* resource, const int64_t* shape, int shapeCount)
            -> winrt::Windows::AI::MachineLearning::TensorFloat
        {
            winrt::com_ptr<IUnknown> tensorUnknown;
            ThrowIfFailed(tensorFactory->CreateFromD3D12Resource(resource, const_cast<int64_t*>(shape), shapeCount, tensorUnknown.put()));
            return tensorUnknown.as<winrt::Windows::AI::MachineLearning::TensorFloat>();
        };

        auto imageTensor = createTensorFromResource(m_ImageTensorBuffer.resource.Get(), imageShape, _countof(imageShape));
        auto depthTensor = createTensorFromResource(m_DepthTensorBuffer.resource.Get(), depthShape, _countof(depthShape));
        auto normalTensor = createTensorFromResource(m_NormalTensorBuffer.resource.Get(), imageShape, _countof(imageShape));
        auto outputTensor = createTensorFromResource(m_OutputTensorBuffer.resource.Get(), imageShape, _countof(imageShape));

        winrt::Windows::Foundation::Collections::PropertySet outputBindProperties;
        outputBindProperties.Insert(L"DisableTensorCpuSync", winrt::box_value(true));

        m_Binding.Clear();
        m_Binding.Bind(m_InputNames[0], imageTensor);
        m_Binding.Bind(m_InputNames[1], depthTensor);
        m_Binding.Bind(m_InputNames[2], normalTensor);
        m_Binding.Bind(m_OutputName, outputTensor, outputBindProperties);

        m_Session.Evaluate(m_Binding, L"LulludensWinML");
        return true;
    }
    catch (const winrt::hresult_error& ex) {
        DebugLogA(std::string("[WinML] Inference HRESULT exception: ") + winrt::to_string(ex.message()) + "\n");
        return false;
    }
    catch (const std::exception& ex) {
        DebugLogA(std::string("[WinML] Inference standard exception: ") + ex.what() + "\n");
        return false;
    }
#else
    return false;
#endif
}

void WinMLStyleTransferSystem::DispatchOutputDetensorization()
{
    auto* commandList = m_RendererCore->GetCommandList();
    const std::array<uint32_t, 4> constants = {
        m_Config.inputWidth,
        m_Config.inputHeight,
        m_RenderWidth,
        m_RenderHeight
    };

    ID3D12DescriptorHeap* heaps[] = { m_DetensorizeHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);
    commandList->SetComputeRootSignature(m_DetensorizeRootSignature.Get());
    commandList->SetComputeRoot32BitConstants(0, static_cast<UINT>(constants.size()), constants.data(), 0);

    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = m_DetensorizeHeap->GetGPUDescriptorHandleForHeapStart();
    commandList->SetComputeRootDescriptorTable(1, srvHandle);

    const UINT descriptorSize = m_RendererCore->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_GPU_DESCRIPTOR_HANDLE uavHandle = srvHandle;
    uavHandle.ptr += descriptorSize;
    commandList->SetComputeRootDescriptorTable(2, uavHandle);

    commandList->SetPipelineState(m_DetensorizePSO.Get());
    commandList->Dispatch((m_RenderWidth + (kThreadsX - 1)) / kThreadsX, (m_RenderHeight + (kThreadsY - 1)) / kThreadsY, 1);
}

bool WinMLStyleTransferSystem::UploadOutput()
{
    auto* commandList = m_RendererCore->GetCommandList();

    TransitionResource(commandList, m_OutputTensorBuffer.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

    if (m_OutputReady) {
        TransitionResource(commandList, m_OutputTexture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }

    DispatchOutputDetensorization();

    D3D12_RESOURCE_BARRIER uavBarrier = {};
    uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    uavBarrier.UAV.pResource = m_OutputTexture.Get();
    commandList->ResourceBarrier(1, &uavBarrier);

    TransitionResource(commandList, m_OutputTensorBuffer.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    TransitionResource(commandList, m_OutputTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);

    m_OutputReady = true;
    return true;
}

std::wstring WinMLStyleTransferSystem::GetDefaultModelPath() const
{
    return L"C:\\LocalRepository\\CapstoneDesign\\Learning\\net4\\net4.onnx";
}

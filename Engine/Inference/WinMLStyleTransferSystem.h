#pragma once

#include "Core/ISystem.h"
#include "..\..\Common\d3dUtil.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class RendererCore;

#if __has_include(<winrt/base.h>) && __has_include(<winrt/Windows.AI.MachineLearning.h>)
#define LULLUDENS_HAS_WINML_STYLE 1
#include <winrt/base.h>
#include <winrt/Windows.AI.MachineLearning.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <windows.ai.machinelearning.native.h>
#endif

class WinMLStyleTransferSystem : public ISystem
{
public:
    struct Config
    {
        std::wstring modelPath;
        uint32_t inputWidth = 640;
        uint32_t inputHeight = 360;
    };

    WinMLStyleTransferSystem(RendererCore* rendererCore, Config config);

    void Initialize() override;
    void Update(float deltaTime) override;
    void Shutdown() override;
    const char* GetName() const override { return "WinMLStyleTransferSystem"; }

    bool Execute();
    bool IsReady() const { return m_BackendReady && m_ModelReady; }
    ID3D12Resource* GetOutputTexture() const { return m_OutputTexture.Get(); }

    struct GpuBuffer
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        uint32_t elementCount = 0;
        uint32_t elementStride = 0;
    };

private:
    void CreateOutputTexture();
    void CreateTensorResources();
    void CreateComputePipeline();
    void CreateDescriptorHeaps();
    bool LoadBackend();
    bool CaptureInputs();
    bool RunInference();
    bool UploadOutput();
    void DispatchTensorization();
    void DispatchOutputDetensorization();
    void CopyOutputTensorToPreviousInput();
    std::wstring GetDefaultModelPath() const;

private:
    RendererCore* m_RendererCore = nullptr;
    Config m_Config;
    uint32_t m_RenderWidth = 0;
    uint32_t m_RenderHeight = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_OutputTexture;
    GpuBuffer m_ImageTensorBuffer;
    GpuBuffer m_DepthTensorBuffer;
    GpuBuffer m_NormalTensorBuffer;
    GpuBuffer m_PreviousStylizedTensorBuffer;
    GpuBuffer m_OutputTensorBuffer;
    GpuBuffer m_DepthMinMaxBuffer;

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_TensorizeHeap;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_DetensorizeHeap;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_TensorizeRootSignature;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_DetensorizeRootSignature;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_ResetDepthMinMaxPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_TensorizePSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_NormalizeDepthPSO;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_DetensorizePSO;

    bool m_BackendReady = false;
    bool m_ModelReady = false;
    bool m_OutputReady = false;
    bool m_InputBuffersNeedUavTransition = false;
    bool m_HasPreviousStylizedInput = false;

#if defined(LULLUDENS_HAS_WINML_STYLE)
    winrt::Windows::AI::MachineLearning::LearningModel m_Model{ nullptr };
    winrt::Windows::AI::MachineLearning::LearningModelDevice m_Device{ nullptr };
    winrt::Windows::AI::MachineLearning::LearningModelSession m_Session{ nullptr };
    winrt::Windows::AI::MachineLearning::LearningModelBinding m_Binding{ nullptr };
    std::vector<winrt::hstring> m_InputNames;
    winrt::hstring m_OutputName;
#endif
};

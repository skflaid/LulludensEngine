#pragma once

#include "Core/ISystem.h"
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

private:
    struct StagingBuffer
    {
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        UINT numRows = 0;
        UINT64 rowSizeInBytes = 0;
        UINT64 totalBytes = 0;
    };

    void CreateOutputTexture();
    void CreateStagingBuffers();
    bool LoadBackend();
    bool CaptureInputs();
    bool RunInference();
    bool UploadOutput();
    std::wstring GetDefaultModelPath() const;

private:
    RendererCore* m_RendererCore = nullptr;
    Config m_Config;
    uint32_t m_RenderWidth = 0;
    uint32_t m_RenderHeight = 0;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_OutputTexture;
    StagingBuffer m_LightingReadback;
    StagingBuffer m_DepthReadback;
    StagingBuffer m_NormalReadback;
    StagingBuffer m_OutputUpload;

    std::vector<float> m_ImageTensor;
    std::vector<float> m_DepthTensor;
    std::vector<float> m_NormalTensor;
    std::vector<uint8_t> m_OutputPixels;

    bool m_BackendReady = false;
    bool m_ModelReady = false;
    bool m_OutputReady = false;

#if defined(LULLUDENS_HAS_WINML_STYLE)
    winrt::Windows::AI::MachineLearning::LearningModel m_Model{ nullptr };
    winrt::Windows::AI::MachineLearning::LearningModelDevice m_Device{ nullptr };
    winrt::Windows::AI::MachineLearning::LearningModelSession m_Session{ nullptr };
    winrt::Windows::AI::MachineLearning::LearningModelBinding m_Binding{ nullptr };
    std::vector<winrt::hstring> m_InputNames;
    winrt::hstring m_OutputName;
#endif
};

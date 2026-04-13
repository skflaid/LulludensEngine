#pragma once

#include "Core/ISystem.h"
#include <d3d12.h>
#include <wrl/client.h>
#include <cstdint>
#include <string>
#include <vector>

class RendererCore;

#if defined(LULLUDENS_ENABLE_DIRECTML) && __has_include(<onnxruntime_cxx_api.h>) && __has_include(<dml_provider_factory.h>)
#define LULLUDENS_HAS_DIRECTML_STYLE 1
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>
#endif

class DirectMLStyleTransferSystem : public ISystem
{
public:
    struct Config
    {
        // ONNX로 export된 스타일 트랜스퍼 모델 경로.
        std::wstring modelPath;
        // 모델이 기대하는 입력 텍스처 너비.
        uint32_t inputWidth = 640;
        // 모델이 기대하는 입력 텍스처 높이.
        uint32_t inputHeight = 360;
    };

    // RendererCore와 모델 설정을 받아 후처리형 스타일 시스템을 구성한다.
    DirectMLStyleTransferSystem(RendererCore* rendererCore, Config config);

    // 출력 텍스처/스테이징 버퍼/DirectML 백엔드를 초기화한다.
    void Initialize() override;
    // 현재 구현에서는 프레임별 상태만 유지하므로 별도 업데이트는 없다.
    void Update(float deltaTime) override;
    // GPU/CPU 추론 자원을 해제한다.
    void Shutdown() override;
    const char* GetName() const override { return "DirectMLStyleTransferSystem"; }

    // Lighting + Depth + Normal을 읽어 스타일 추론을 수행한다.
    bool Execute();
    // DirectML 백엔드와 모델 세션이 모두 준비되었는지 반환한다.
    bool IsReady() const { return m_BackendReady && m_ModelReady; }
    // 마지막 추론 결과가 기록된 출력 텍스처를 반환한다.
    ID3D12Resource* GetOutputTexture() const { return m_OutputTexture.Get(); }

private:
    struct StagingBuffer
    {
        // GPU 텍스처를 CPU로 읽거나 CPU 결과를 GPU로 올릴 때 사용하는 버퍼 리소스.
        Microsoft::WRL::ComPtr<ID3D12Resource> resource;
        // CopyTextureRegion에 필요한 footprint 정보.
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint = {};
        // footprint가 차지하는 텍셀 행 수.
        UINT numRows = 0;
        // 실제 유효 row 바이트 수.
        UINT64 rowSizeInBytes = 0;
        // 할당된 총 버퍼 크기.
        UINT64 totalBytes = 0;
    };

    // 스타일 결과를 담을 GPU 출력 텍스처를 만든다.
    void CreateOutputTexture();
    // 입력 readback/출력 upload용 staging buffer를 만든다.
    void CreateStagingBuffers();
    // ONNX Runtime + DirectML 실행 백엔드를 로드한다.
    bool LoadBackend();
    // Lighting/Depth/Normal 텍스처를 CPU 텐서로 변환한다.
    bool CaptureInputs();
    // CPU 텐서를 DirectML 세션에 넣어 추론을 실행한다.
    bool RunInference();
    // 추론 결과를 GPU 텍스처로 업로드한다.
    bool UploadOutput();
    // 모델 경로가 따로 주어지지 않았을 때 사용할 기본 ONNX 경로를 만든다.
    std::wstring GetDefaultModelPath() const;

private:
    // 렌더러 리소스 접근용 코어 포인터.
    RendererCore* m_RendererCore = nullptr;
    // 모델 경로와 입력 해상도 설정.
    Config m_Config;

    // 최종 스타일 결과가 저장되는 GPU 텍스처.
    Microsoft::WRL::ComPtr<ID3D12Resource> m_OutputTexture;
    // Lighting 버퍼를 CPU로 읽기 위한 readback 버퍼.
    StagingBuffer m_LightingReadback;
    // Depth 버퍼를 CPU로 읽기 위한 readback 버퍼.
    StagingBuffer m_DepthReadback;
    // Normal 버퍼를 CPU로 읽기 위한 readback 버퍼.
    StagingBuffer m_NormalReadback;
    // 추론 결과를 GPU 텍스처로 다시 업로드하기 위한 upload 버퍼.
    StagingBuffer m_OutputUpload;

    // RGB 입력을 NCHW float tensor로 담는 버퍼.
    std::vector<float> m_ImageTensor;
    // Depth 입력을 NCHW float tensor로 담는 버퍼.
    std::vector<float> m_DepthTensor;
    // Normal 입력을 NCHW float tensor로 담는 버퍼.
    std::vector<float> m_NormalTensor;
    // 추론 결과 RGBA 바이트 배열.
    std::vector<uint8_t> m_OutputPixels;

    // 실행 백엔드 사용 가능 여부.
    bool m_BackendReady = false;
    // ONNX 모델 세션 생성 성공 여부.
    bool m_ModelReady = false;
    // 출력 텍스처가 이전 프레임 결과를 보유 중인지 여부.
    bool m_OutputReady = false;

#if defined(LULLUDENS_HAS_DIRECTML_STYLE)
    // ONNX Runtime 전역 환경 객체.
    std::unique_ptr<Ort::Env> m_OrtEnv;
    // DirectML 실행 공급자가 연결된 ONNX 세션.
    std::unique_ptr<Ort::Session> m_OrtSession;
    // 입력 이름의 수명 보관용 문자열 배열.
    std::vector<std::string> m_InputNames;
    // Run 호출에 바로 넘기는 입력 이름 포인터 배열.
    std::vector<const char*> m_InputNameViews;
    // 출력 이름의 수명 보관용 문자열 배열.
    std::vector<std::string> m_OutputNames;
    // Run 호출에 바로 넘기는 출력 이름 포인터 배열.
    std::vector<const char*> m_OutputNameViews;
#endif
};

// Renderer/Systems/RenderSystem.h
#pragma once

#include "Core/ISystem.h"
#include "Core/Entity.h"
#include "Renderer/RendererCore.h"
#include "Renderer/RenderCommandQueue.h"
#include "Renderer/RenderSnapshot.h"
#include "Renderer/RenderSnapshotBuilder.h"
#include "RenderConstants.h"

#include <DirectXMath.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

using namespace DirectX;
static constexpr UINT MAX_BONES = 128;

class GameEngine;
class DirectSRUpscaler;
class WinMLStyleTransferSystem;
class EnvironmentManager;
class SkyRenderer;
struct MaterialComponent;
struct MeshComponent;
struct TransformComponent;

enum class RenderMode {
    Composite,    // Lighting + SSGI 합성 결과.
    Lighting,     // Lighting 결과만 표시.
    SSGI,         // SSGI 결과만 표시.
    MotionVector  // 모션 벡터 디버그 시각화.
};

class RenderSystem : public ISystem {
public:
    // 엔진/창 정보와 렌더 해상도를 받아 deferred renderer를 구성한다.
    RenderSystem(GameEngine* engine, HWND hwnd, uint32_t width, uint32_t height);
    ~RenderSystem();

    // 렌더 코어, 파이프라인 상태, 후처리/환경 렌더링 시스템을 초기화한다.
    void Initialize() override;
    // 렌더 시스템의 프레임 시간만 누적한다. 실제 draw는 Render()에서 수행한다.
    void Update(float deltaTime) override;
    // 상수 버퍼와 렌더링 하위 시스템을 정리한다.
    void Shutdown() override;

    uint32_t GetWidth() const { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }

    const char* GetName() const override { return "RenderSystem"; }

    void RegisterEntity(Entity* entity);
    void UnregisterEntity(Entity* entity);
    void SetActiveSky(Entity* skyEntity);

    // 한 프레임의 Shadow/G-Buffer/MotionVector/SSGI/Lighting/StyleTransfer 흐름을 실행한다.
    void Render();
    // Composite/Lighting/SSGI/MotionVector 디버그 표시 모드를 순환한다.
    void ToggleRenderMode();
    void ToggleStyleTransfer();
    RenderMode GetRenderMode() const;

private:
    // 지오메트리 속성을 여러 MRT에 기록하는 G-Buffer 패스.
    void RenderGBufferPass(UINT frameIndex);
    // 배경 픽셀의 normal/depth 값을 채워 후속 패스 샘플링을 안정화한다.
    void RenderBackgroundResolvePass(UINT frameIndex);
    // 조명 결과를 오프스크린 LightingBuffer에 출력하는 패스.
    void RenderLightingPass(UINT frameIndex);
    // 화면 공간 간접광을 계산하는 SSGI compute 패스.
    void RenderSSGIPass(UINT frameIndex);
    // SSGI 결과에 bilateral/temporal denoise를 적용한다.
    void RenderSSGIDenoisePass(UINT frameIndex);
    // temporal filter를 위해 현재 SSGI 결과를 이전 프레임 버퍼로 복사한다.
    void CopySSGIToPrevious(UINT frameIndex);
    // Lighting 또는 StyleTransfer 결과를 백버퍼로 복사한다.
    void CopyFrameToBackBuffer(ID3D12Resource* sourceTexture);
    // StyleTransfer 결과를 DirectSR로 업스케일하고 성공하면 백버퍼로 출력한다.
    bool TryUpscaleStyleTransferOutput(ID3D12Resource* styleOutputTexture);
    // DirectSR와 motion vector debug view가 공유하는 velocity 텍스처를 만든다.
    void CreateDirectSRResources();
    // 현재/이전 transform 및 view-projection 차이로 motion vector 텍스처를 렌더링한다.
    void RenderVelocityPass(UINT frameIndex);
    // motion vector 텍스처를 HSV 색상으로 변환해 백버퍼에 직접 표시한다.
    void RenderMotionVectorVisualizationPass();
    // 활성 SkyComponent가 있으면 LightingBuffer 위에 skybox를 합성한다.
    void RenderSkyPass(UINT frameIndex);
    // 실제 ECS 엔티티 1개를 G-Buffer 패스에 그린다.
    void RenderEntity(Entity* entity, UINT frameIndex, int objectIndex);
    void RenderEntity(Entity* entity, UINT frameIndex, int objectIndex, const XMFLOAT4X4* snapshotWorld);
    struct RenderProxy;
    // 렌더 스레드가 소유한 proxy 정보를 사용해 G-Buffer draw를 수행한다.
    void RenderProxyItem(const RenderProxy& proxy, UINT frameIndex, int objectIndex, const XMFLOAT4X4* snapshotWorld);
    // 동일한 proxy를 velocity pass용 root signature/PSO로 그린다.
    void RenderVelocityProxyItem(const RenderProxy& proxy, UINT frameIndex, int objectIndex, const XMFLOAT4X4* snapshotWorld);
    // 카메라/라이트/그림자/렌더 모드 상수를 프레임 CB에 채운다.
    void UpdatePassConstants(UINT frameIndex);
    // 렌더 스레드가 매 프레임 사용할 physics/game snapshot을 갱신한다.
    void RefreshRenderSnapshot();
    // 현재 프레임 보간 transform을 proxy ID로 조회한다.
    const XMFLOAT4X4* FindSnapshotWorld(uint32_t entityId) const;
    // 이전 프레임 transform을 proxy ID로 조회해 velocity 계산에 사용한다.
    const XMFLOAT4X4* FindPreviousSnapshotWorld(uint32_t entityId) const;
    // game/physics 스레드에서 큐잉한 렌더 명령을 draw 전에 적용한다.
    void ProcessRenderCommands();
    // CPU mesh data만 가진 MeshComponent에 D3D12 vertex/index buffer를 생성한다.
    void EnsureMeshResources(MeshComponent* mesh);

    void RenderShadowPass(UINT frameIndex);
    void CreateShadowPipelineState();
    void CreateShadowResources();

    void CreateGBufferPipelineState();
    void CreateVelocityPipelineState();
    void CreateMotionVectorDebugPipelineState();
    void CreateLightingPipelineState();
    void CreateBackgroundResolvePipelineState();
    void CreateSSGIPipelineState();
    void CreateSSGIDenoisePipelineState();
    void CreateConstantBuffer();
    void InitializeTextures();

private:
    GameEngine* m_Engine = nullptr; // GameEngine 소유자는 아니며 상태 조회에만 사용한다.

    // 스왑체인, G-Buffer, 오프스크린 버퍼를 소유하는 저수준 렌더 코어.
    std::unique_ptr<RendererCore> m_RendererCore;
    // Lighting 이후 화면 스타일 추론을 담당하는 후처리 시스템.
    std::unique_ptr<WinMLStyleTransferSystem> m_WinMLStyleTransferSystem;
    // StyleTransfer 결과를 백버퍼 크기로 업스케일한다.
    std::unique_ptr<DirectSRUpscaler> m_DirectSRUpscaler;
    // SkyComponent의 활성 환경을 관리하고 cubemap 텍스처를 조회한다.
    std::unique_ptr<EnvironmentManager> m_EnvironmentManager;
    // Skybox 전용 root signature/PSO/상수 버퍼를 소유한다.
    std::unique_ptr<SkyRenderer> m_SkyRenderer;
    std::vector<Entity*> m_RenderableEntities;

    struct RenderProxy {
        // 렌더 스레드가 draw에 필요한 컴포넌트 포인터만 보관하는 사본.
        // 생산자 스레드는 command queue만 갱신하고, draw 패스는 이 배열만 순회한다.
        uint32_t Id = 0;
        TransformComponent* Transform = nullptr;
        MeshComponent* Mesh = nullptr;
        MaterialComponent* Material = nullptr;
        bool Active = true;
    };

    // 스레드 간 큐: 등록/삭제/머티리얼 변경 요청을 RenderProxy 목록으로 반영한다.
    RenderCommandQueue m_RenderCommandQueue;
    std::vector<RenderProxy> m_RenderProxies;
    // draw 전에 physics snapshot과 camera state로 만든 현재 프레임 값 스냅샷.
    RenderSnapshotBuilder m_RenderSnapshotBuilder;
    RenderSnapshot m_CurrentRenderSnapshot;
    std::unordered_map<uint32_t, XMFLOAT4X4> m_SnapshotWorldByEntity;
    std::unordered_map<uint32_t, XMFLOAT4X4> m_PreviousSnapshotWorldByEntity;
    HWND m_Hwnd;
    uint32_t m_Width;
    uint32_t m_Height;

    // Deferred rendering에 필요한 root signature와 pipeline state 모음.
    ComPtr<ID3D12RootSignature> m_GBufferRootSignature;
    ComPtr<ID3D12PipelineState> m_GBufferPipelineState;
    ComPtr<ID3D12RootSignature> m_VelocityRootSignature;
    ComPtr<ID3D12PipelineState> m_VelocityPipelineState;
    ComPtr<ID3D12RootSignature> m_MotionVectorDebugRootSignature;
    ComPtr<ID3D12PipelineState> m_MotionVectorDebugPipelineState;

    ComPtr<ID3D12PipelineState> m_ShadowPipelineState;

    ComPtr<ID3D12RootSignature> m_LightingRootSignature;
    ComPtr<ID3D12PipelineState> m_LightingPipelineState;
    ComPtr<ID3D12RootSignature> m_BackgroundResolveRootSignature;
    ComPtr<ID3D12PipelineState> m_BackgroundResolvePipelineState;
    ComPtr<ID3D12RootSignature> m_SSGIRootSignature;
    ComPtr<ID3D12PipelineState> m_SSGIPipelineState;
    ComPtr<ID3D12RootSignature> m_SSGIDenoiseRootSignature;
    ComPtr<ID3D12PipelineState> m_SSGIDenoisePipelineState;

    // Shadow map 리소스와 DSV.
    ComPtr<ID3D12Resource> m_ShadowMap;
    ComPtr<ID3D12DescriptorHeap> m_ShadowDsvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_ShadowDsv = {};
    UINT m_ShadowMapSize = 2048;
    D3D12_VIEWPORT m_ShadowViewport = {};
    D3D12_RECT m_ShadowScissorRect = {};

    // DirectSR 입력 및 motion vector debug view가 공유하는 velocity texture.
    ComPtr<ID3D12Resource> m_DirectSRMotionVectors;
    ComPtr<ID3D12DescriptorHeap> m_DirectSRMotionVectorRTVHeap;
    ComPtr<ID3D12DescriptorHeap> m_DirectSRMotionVectorSRVHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_DirectSRMotionVectorRTV = {};
    uint32_t m_StyleOutputWidth = 0;
    uint32_t m_StyleOutputHeight = 0;

    // motion vector 계산용 현재/이전 카메라 행렬.
    XMFLOAT4X4 m_ViewMatrix;
    XMFLOAT4X4 m_ProjMatrix;
    XMFLOAT4X4 m_CurrentViewProjMatrix;
    XMFLOAT4X4 m_PreviousViewProjMatrix;
    bool m_HasCurrentViewProjMatrix = false;
    bool m_HasPreviousViewProjMatrix = false;

    // 조명 패스에서 사용하는 기본 프레임 상수.
    XMFLOAT4 m_AmbientLight = { 0.6f, 0.6f, 0.6f, 1.0f };
    float m_TotalTime = 0.0f;
    float m_DeltaTime = 0.0f;

    // 렌더 표시 모드와 후처리 토글. UI/윈도우 스레드에서도 접근하므로 mutex로 보호한다.
    mutable std::mutex m_SettingsMutex;
    RenderMode m_RenderMode = RenderMode::Composite;

    // 첫 SSGI 프레임은 리소스가 이미 UAV 상태로 시작하므로 barrier를 생략한다.
    bool m_IsFirstSSGIFrame = true;
    // 첫 G-Buffer 프레임은 초기 상태가 render target이므로 SRV -> RTV barrier가 필요 없다.
    bool m_IsFirstGBufferFrame = true;
    // LightingBuffer가 첫 프레임 이후 COPY_SOURCE -> RENDER_TARGET 전환이 필요한지 추적한다.
    bool m_IsFirstLightingFrame = true;
    bool m_IsStyleTransferEnabled = true;
    bool m_DirectSRResetHistory = true;

    // 프레임별 업로드 상수 버퍼.
    static constexpr int FrameCount = static_cast<int>(RendererFrameCount);

    // 오브젝트별 상수 버퍼 (b0).
    ComPtr<ID3D12Resource> m_ObjectConstantBuffers[FrameCount];
    UINT8* m_ObjectConstantBufferDataBegin[FrameCount];
    UINT m_ObjectConstantBufferSize;

    // 머티리얼 상수 버퍼 (b1).
    ComPtr<ID3D12Resource> m_MaterialConstantBuffers[FrameCount];
    UINT8* m_MaterialConstantBufferDataBegin[FrameCount];
    UINT m_MaterialConstantBufferSize;

    // 패스 상수 버퍼 (b2).
    ComPtr<ID3D12Resource> m_PassConstantBuffers[FrameCount];
    UINT8* m_PassConstantBufferDataBegin[FrameCount];
    UINT m_PassConstantBufferSize;

    // velocity pass 상수 버퍼 (Velocity.hlsl의 b1).
    ComPtr<ID3D12Resource> m_VelocityPassConstantBuffers[FrameCount];
    UINT8* m_VelocityPassConstantBufferDataBegin[FrameCount] = {};
    UINT m_VelocityPassConstantBufferSize = 0;

    // 스키닝 상수 버퍼 (b3).
    UINT m_SkinningConstantBufferSize = 0;
    ComPtr<ID3D12Resource> m_SkinningConstantBuffers[FrameCount];
    BYTE* m_SkinningConstantBufferDataBegin[FrameCount]{};
};

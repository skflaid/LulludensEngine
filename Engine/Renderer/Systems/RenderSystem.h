// Renderer/Systems/RenderSystem.h
#pragma once
#include "Core/ISystem.h"
#include "Core/Entity.h"
#include "Renderer/RendererCore.h"
#include "Renderer/RenderCommandQueue.h"
#include "Renderer/RenderSnapshot.h"
#include "Renderer/RenderSnapshotBuilder.h"
#include "RenderConstants.h"
#include <vector>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <DirectXMath.h>

using namespace DirectX;
static constexpr UINT MAX_BONES = 128;

class GameEngine;
class WinMLStyleTransferSystem;
class EnvironmentManager;
class SkyRenderer;
struct MaterialComponent;
struct MeshComponent;
struct TransformComponent;

enum class RenderMode {
    Composite,  // Lighting + SSGI
    Lighting,   // Lighting only
    SSGI        // SSGI only
};

class RenderSystem : public ISystem {
public:
    // 엔진/창 정보와 렌더 해상도를 받아 deferred renderer를 구성한다.
    RenderSystem(GameEngine* engine, HWND hwnd, uint32_t width, uint32_t height);
    ~RenderSystem();

    // 렌더 코어, 파이프라인 상태, 스타일 트랜스퍼 시스템을 초기화한다.
    void Initialize() override;
    // 현재 렌더 시스템은 별도 게임 로직 업데이트 없이 패스 실행 준비만 담당한다.
    void Update(float deltaTime) override;
    // 상수 버퍼와 하위 시스템을 정리한다.
    void Shutdown() override;

    uint32_t GetWidth() const { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }

    const char* GetName() const override { return "RenderSystem"; }

    void RegisterEntity(Entity* entity);
    void UnregisterEntity(Entity* entity);
    void SetActiveSky(Entity* skyEntity);

    // 한 프레임의 그림자/GBuffer/SSGI/Lighting/StyleTransfer 흐름을 실행한다.
    void Render();
    // Composite/Lighting/SSGI 디버그 표시 모드를 순환한다.
    void ToggleRenderMode();
    void ToggleStyleTransfer();
    RenderMode GetRenderMode() const;

private:
    // 지오메트리를 여러 MRT에 기록하는 G-Buffer 패스.
    void RenderGBufferPass(UINT frameIndex);
    // 조명 결과를 오프스크린 LightingBuffer에 출력하는 패스.
    void RenderLightingPass(UINT frameIndex);
    void RenderBackgroundResolvePass(UINT frameIndex);
    // 간단한 SSGI 계산 패스.
    void RenderSSGIPass(UINT frameIndex);
    // SSGI 결과를 정리하는 denoise 패스.
    void RenderSSGIDenoisePass(UINT frameIndex);
    // temporal filter를 위해 현재 SSGI 결과를 이전 프레임 버퍼로 복사한다.
    void CopySSGIToPrevious(UINT frameIndex);
    // Lighting 또는 StyleTransfer 결과를 백버퍼로 복사한다.
    void CopyFrameToBackBuffer(ID3D12Resource* sourceTexture);
    void RenderSkyPass(UINT frameIndex);
    // 실제 메시 엔티티 1개를 G-Buffer 패스에 그린다.
    void RenderEntity(Entity* entity, UINT frameIndex, int objectIndex);
    void RenderEntity(Entity* entity, UINT frameIndex, int objectIndex, const XMFLOAT4X4* snapshotWorld);
    struct RenderProxy;
    void RenderProxyItem(const RenderProxy& proxy, UINT frameIndex, int objectIndex, const XMFLOAT4X4* snapshotWorld);
    // 카메라/라이트/그림자/모드 상수를 프레임 CB에 채운다.
    void UpdatePassConstants(UINT frameIndex);
    // Render-thread entry point for physics/game snapshots each frame.
    void RefreshRenderSnapshot();
    // Looks up interpolated physics world matrices for render proxies.
    const XMFLOAT4X4* FindSnapshotWorld(uint32_t entityId) const;
    // Applies queued cross-thread render commands before drawing.
    void ProcessRenderCommands();
    void EnsureMeshResources(MeshComponent* mesh);

    void RenderShadowPass(UINT frameIndex);
    void CreateShadowPipelineState();
    void CreateShadowResources();

    void CreateGBufferPipelineState();
    void CreateLightingPipelineState();
    void CreateBackgroundResolvePipelineState();
    void CreateSSGIPipelineState();
    void CreateSSGIDenoisePipelineState();
    void CreateConstantBuffer();
    void InitializeTextures();

private:
    GameEngine* m_Engine = nullptr; // GameEngine 포인터 멤버

    // 스왑체인/GBuffer/오프스크린 버퍼를 소유하는 저수준 렌더 코어.
    std::unique_ptr<RendererCore> m_RendererCore;
    // Lighting 이후 화면 스타일 추론을 담당하는 후처리 시스템.
    std::unique_ptr<WinMLStyleTransferSystem> m_WinMLStyleTransferSystem;
    std::unique_ptr<EnvironmentManager> m_EnvironmentManager;
    std::unique_ptr<SkyRenderer> m_SkyRenderer;
    std::vector<Entity*> m_RenderableEntities;
    struct RenderProxy {
        // Render-owned copy of the component pointers needed for drawing.
        // Commands update this list; draw passes iterate it without touching
        // the producer queue.
        uint32_t Id = 0;
        TransformComponent* Transform = nullptr;
        MeshComponent* Mesh = nullptr;
        MaterialComponent* Material = nullptr;
        bool Active = true;
    };
    // Cross-thread queue: producers enqueue registration/material changes,
    // render thread drains and converts them into RenderProxy entries.
    RenderCommandQueue m_RenderCommandQueue;
    std::vector<RenderProxy> m_RenderProxies;
    // Current frame's value snapshot built from physics snapshots and camera
    // state before the draw passes execute.
    RenderSnapshotBuilder m_RenderSnapshotBuilder;
    RenderSnapshot m_CurrentRenderSnapshot;
    std::unordered_map<uint32_t, XMFLOAT4X4> m_SnapshotWorldByEntity;
    HWND m_Hwnd;
    uint32_t m_Width;
    uint32_t m_Height;

    // Pipeline states for Deferred Rendering
    ComPtr<ID3D12RootSignature> m_GBufferRootSignature;
    ComPtr<ID3D12PipelineState> m_GBufferPipelineState;

    ComPtr<ID3D12PipelineState> m_ShadowPipelineState;

    ComPtr<ID3D12RootSignature> m_LightingRootSignature;
    ComPtr<ID3D12PipelineState> m_LightingPipelineState;
    ComPtr<ID3D12RootSignature> m_BackgroundResolveRootSignature;
    ComPtr<ID3D12PipelineState> m_BackgroundResolvePipelineState;
    ComPtr<ID3D12RootSignature> m_SSGIRootSignature;
    ComPtr<ID3D12PipelineState> m_SSGIPipelineState;
    ComPtr<ID3D12RootSignature> m_SSGIDenoiseRootSignature;
    ComPtr<ID3D12PipelineState> m_SSGIDenoisePipelineState;

    // Shadow map 리소스 + DSV
    ComPtr<ID3D12Resource> m_ShadowMap;
    ComPtr<ID3D12DescriptorHeap> m_ShadowDsvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_ShadowDsv = {};
    UINT m_ShadowMapSize = 2048;
    D3D12_VIEWPORT m_ShadowViewport = {};
    D3D12_RECT m_ShadowScissorRect = {};

    // Camera matrices
    XMFLOAT4X4 m_ViewMatrix;
    XMFLOAT4X4 m_ProjMatrix;
    
    // Lighting
    XMFLOAT4 m_AmbientLight = { 0.6f, 0.6f, 0.6f, 1.0f };
    float m_TotalTime = 0.0f;
    float m_DeltaTime = 0.0f;
    
    // Render mode
    mutable std::mutex m_SettingsMutex;
    RenderMode m_RenderMode = RenderMode::Composite;
    
    // First frame flag for SSGI barrier
    bool m_IsFirstSSGIFrame = true;
    // First frame flag for G-Buffer barrier
    bool m_IsFirstGBufferFrame = true;
    // LightingBuffer가 첫 프레임 이후 COPY_SOURCE -> RENDER_TARGET 전환이 필요한지 추적한다.
    bool m_IsFirstLightingFrame = true;
    bool m_IsStyleTransferEnabled = true;

    // Constant buffers
    static constexpr int FrameCount = static_cast<int>(RendererFrameCount);
    
    // Per-object constant buffer (b0)
    ComPtr<ID3D12Resource> m_ObjectConstantBuffers[FrameCount];
    UINT8* m_ObjectConstantBufferDataBegin[FrameCount];
    UINT m_ObjectConstantBufferSize;
    
    // Material constant buffer (b1)
    ComPtr<ID3D12Resource> m_MaterialConstantBuffers[FrameCount];
    UINT8* m_MaterialConstantBufferDataBegin[FrameCount];
    UINT m_MaterialConstantBufferSize;
    
    // Pass constant buffer (b2)
    ComPtr<ID3D12Resource> m_PassConstantBuffers[FrameCount];
    UINT8* m_PassConstantBufferDataBegin[FrameCount];
    UINT m_PassConstantBufferSize;

    // skinned (b3)
    UINT m_SkinningConstantBufferSize = 0;
    ComPtr<ID3D12Resource> m_SkinningConstantBuffers[FrameCount];
    BYTE* m_SkinningConstantBufferDataBegin[FrameCount]{};
};

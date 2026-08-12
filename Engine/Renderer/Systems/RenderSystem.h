// Renderer/Systems/RenderSystem.h
#pragma once
#include "Core/ISystem.h"
#include "Core/Entity.h"
#include "Renderer/RendererCore.h"
#include "RenderConstants.h"
#include <vector>
#include <memory>
#include <DirectXMath.h>

using namespace DirectX;
static constexpr UINT MAX_BONES = 128;

class GameEngine;
class EditorUI;

enum class RenderMode {
    Composite,  // Lighting + SSGI
    Lighting,   // Lighting only
    SSGI        // SSGI only
};

class RenderSystem : public ISystem {
public:
    RenderSystem(GameEngine* engine, HWND hwnd, uint32_t width, uint32_t height);
    ~RenderSystem();

    void Initialize() override;
    void Update(float deltaTime) override;
    void Shutdown() override;

    uint32_t GetWidth() const { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }

    const char* GetName() const override { return "RenderSystem"; }

    void RegisterEntity(Entity* entity);
    void UnregisterEntity(Entity* entity);

    void Render();
    bool HandleEditorMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    bool WantsEditorMouse() const;
    bool WantsEditorKeyboard() const;
    bool WantsViewportInput() const;
    bool IsViewportInputArea(int clientX, int clientY) const;
    void ToggleRenderMode();
    RenderMode GetRenderMode() const { return m_RenderMode; }

private:
    void RenderGBufferPass(UINT frameIndex);
    void RenderLightingPass(UINT frameIndex);
    void RenderSSGIPass(UINT frameIndex);
    void RenderSSGIDenoisePass(UINT frameIndex);
    void CopySSGIToPrevious(UINT frameIndex);
    void RenderEntity(Entity* entity, UINT frameIndex, int objectIndex);
    void UpdatePassConstants(UINT frameIndex);

    void RenderShadowPass(UINT frameIndex);
    void CreateShadowPipelineState();
    void CreateShadowResources();

    void CreateGBufferPipelineState();
    void CreateLightingPipelineState();
    void CreateSSGIPipelineState();
    void CreateSSGIDenoisePipelineState();
    void CreateConstantBuffer();
    void InitializeTextures();

private:
    GameEngine* m_Engine = nullptr; // GameEngine 포인터 멤버

    std::unique_ptr<RendererCore> m_RendererCore;
    std::unique_ptr<EditorUI> m_EditorUI;
    std::vector<Entity*> m_RenderableEntities;
    HWND m_Hwnd;
    uint32_t m_Width;
    uint32_t m_Height;

    // Pipeline states for Deferred Rendering
    ComPtr<ID3D12RootSignature> m_GBufferRootSignature;
    ComPtr<ID3D12PipelineState> m_GBufferPipelineState;

    ComPtr<ID3D12PipelineState> m_ShadowPipelineState;

    ComPtr<ID3D12RootSignature> m_LightingRootSignature;
    ComPtr<ID3D12PipelineState> m_LightingPipelineState;
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
    float m_DeltaTime = 1.0f / 60.0f;
    
    // Render mode
    RenderMode m_RenderMode = RenderMode::Composite;
    
    // First frame flag for SSGI barrier
    bool m_IsFirstSSGIFrame = true;
    // First frame flag for G-Buffer barrier
    bool m_IsFirstGBufferFrame = true;

    // Constant buffers
    static const int FrameCount = 2;
    
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

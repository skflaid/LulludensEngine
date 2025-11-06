// Renderer/Systems/RenderSystem.h
#pragma once
#include "Core/ISystem.h"
#include "Core/Entity.h"
#include "Renderer/RendererCore.h"
#include "RenderConstants.h"  // 추가
#include <vector>
#include <memory>
#include <DirectXMath.h>

using namespace DirectX;

class RenderSystem : public ISystem {
public:
    RenderSystem(HWND hwnd, uint32_t width, uint32_t height);
    ~RenderSystem();

    void Initialize() override;
    void Update(float deltaTime) override;
    void Shutdown() override;

    const char* GetName() const override { return "RenderSystem"; }

    void RegisterEntity(Entity* entity);
    void UnregisterEntity(Entity* entity);

    void Render();

private:
    void RenderEntity(Entity* entity, UINT frameIndex, int objectIndex);
    void UpdatePassConstants(UINT frameIndex);
    void CreatePipelineState();
    void CreateConstantBuffer();

private:
    std::unique_ptr<RendererCore> m_RendererCore;
    std::vector<Entity*> m_RenderableEntities;
    HWND m_Hwnd;
    uint32_t m_Width;
    uint32_t m_Height;

    // Pipeline state
    ComPtr<ID3D12RootSignature> m_RootSignature;
    ComPtr<ID3D12PipelineState> m_PipelineState;

    // Camera matrices
    XMFLOAT4X4 m_ViewMatrix;
    XMFLOAT4X4 m_ProjMatrix;
    
    // Lighting
    XMFLOAT4 m_AmbientLight = { 0.6f, 0.6f, 0.6f, 1.0f };
    float m_TotalTime = 0.0f;

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
};

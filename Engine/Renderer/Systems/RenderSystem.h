// Renderer/Systems/RenderSystem.h
#pragma once
#include "Core/ISystem.h"
#include "Core/Entity.h"
#include "Renderer/RendererCore.h"
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
    void RenderEntity(Entity* entity);
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

    // Constant buffer for per-object data
    static const int FrameCount = 2;
    ComPtr<ID3D12Resource> m_ConstantBuffers[FrameCount];
    UINT8* m_ConstantBufferDataBegin[FrameCount];
    UINT m_ConstantBufferSize;

    struct SceneConstants {
        XMFLOAT4X4 world;
        XMFLOAT4X4 view;
        XMFLOAT4X4 proj;
        XMFLOAT4 color;
    };
};

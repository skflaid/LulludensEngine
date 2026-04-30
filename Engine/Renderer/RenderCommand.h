#pragma once

#include "Core/EngineTypes.h"
#include <DirectXMath.h>
#include <string>

struct MaterialComponent;
struct MeshComponent;
struct TransformComponent;

enum class RenderCommandType {
    // Commands describe render-side resource/proxy changes requested by other
    // engine phases, then consumed on the render thread.
    RegisterEntity,
    UnregisterEntity,
    UpdateMesh,
    UpdateMaterial,
    SetActiveCamera,
    Shutdown
};

struct RenderCommand {
    // Keep this as value data plus stable component pointers. The queue copies
    // it across threads before RenderSystem applies it to render-only proxies.
    RenderCommandType Type = RenderCommandType::RegisterEntity;
    EntityId Id = 0;
    std::string MeshResourceName;
    std::string AlbedoTextureName;
    std::string NormalTextureName;
    DirectX::XMFLOAT4 Albedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    TransformComponent* Transform = nullptr;
    MeshComponent* Mesh = nullptr;
    MaterialComponent* Material = nullptr;
    bool Active = true;
};

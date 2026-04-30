#pragma once

#include "Core/EngineTypes.h"
#include "Renderer/CameraState.h"
#include <DirectXMath.h>
#include <vector>

struct RenderDrawItemSnapshot {
    // Render-thread friendly draw state derived from physics snapshots.
    EntityId Id = 0;
    DirectX::XMFLOAT4X4 World;
    bool Visible = true;

    RenderDrawItemSnapshot();
};

class RenderSnapshot {
public:
    // Camera is optional because the first few frames may render before game
    // logic has captured a camera state.
    void SetCamera(const CameraRenderState& camera);
    const CameraRenderState& GetCamera() const { return m_Camera; }
    bool HasCamera() const { return m_HasCamera; }

    void AddDrawItem(const RenderDrawItemSnapshot& item);
    const std::vector<RenderDrawItemSnapshot>& GetDrawItems() const { return m_DrawItems; }
    void Clear();

private:
    CameraRenderState m_Camera;
    std::vector<RenderDrawItemSnapshot> m_DrawItems;
    bool m_HasCamera = false;
};

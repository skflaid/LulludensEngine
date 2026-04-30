#include "RenderSnapshot.h"

using namespace DirectX;

RenderDrawItemSnapshot::RenderDrawItemSnapshot()
{
    XMStoreFloat4x4(&World, XMMatrixIdentity());
}

void RenderSnapshot::SetCamera(const CameraRenderState& camera)
{
    m_Camera = camera;
    m_HasCamera = true;
}

void RenderSnapshot::AddDrawItem(const RenderDrawItemSnapshot& item)
{
    m_DrawItems.push_back(item);
}

void RenderSnapshot::Clear()
{
    m_Camera = CameraRenderState();
    m_HasCamera = false;
    m_DrawItems.clear();
}

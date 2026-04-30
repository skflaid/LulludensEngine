#include "CameraState.h"

using namespace DirectX;

CameraRenderState::CameraRenderState()
{
    XMStoreFloat4x4(&View, XMMatrixIdentity());
    XMStoreFloat4x4(&Proj, XMMatrixIdentity());
    XMStoreFloat4x4(&ViewProj, XMMatrixIdentity());
}


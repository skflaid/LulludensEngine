#include "RenderSnapshotBuilder.h"

#include "Renderer/TransformInterpolator.h"
#include <DirectXMath.h>
#include <unordered_map>

using namespace DirectX;

namespace {
XMFLOAT4X4 BuildWorldMatrix(const PhysicsBodySnapshot& body)
{
    // Convert interpolated snapshot transform into the matrix consumed by the
    // existing D3D12 object constant-buffer path.
    XMMATRIX scale = XMMatrixScalingFromVector(XMLoadFloat3(&body.Scale));
    XMMATRIX rotation = XMMatrixRotationQuaternion(XMLoadFloat4(&body.Rotation));
    XMMATRIX translation = XMMatrixTranslationFromVector(XMLoadFloat3(&body.Position));

    XMFLOAT4X4 world;
    XMStoreFloat4x4(&world, scale * rotation * translation);
    return world;
}
}

RenderSnapshot RenderSnapshotBuilder::Build(
    const PhysicsSnapshotPair& snapshots,
    float alpha,
    const std::optional<CameraLogicState>& cameraState) const
{
    RenderSnapshot renderSnapshot;

    if (cameraState.has_value()) {
        // Camera comes from the game thread as copied value state.
        renderSnapshot.SetCamera(BuildCamera(*cameraState));
    }

    if (!snapshots.HasCurrent()) {
        // No physics tick has been published yet; render can still use fallback
        // live transforms in RenderSystem.
        return renderSnapshot;
    }

    std::unordered_map<EntityId, const PhysicsBodySnapshot*> previousBodies;
    if (snapshots.Previous.has_value()) {
        // Build an id lookup so each current body can find its previous state.
        for (const auto& body : snapshots.Previous->GetBodies()) {
            previousBodies[body.Id] = &body;
        }
    }

    for (const auto& currentBody : snapshots.Current->GetBodies()) {
        PhysicsBodySnapshot interpolated = currentBody;

        auto previousIt = previousBodies.find(currentBody.Id);
        if (previousIt != previousBodies.end()) {
            // Blend between fixed physics ticks using the render-frame alpha.
            interpolated = TransformInterpolator::Interpolate(*previousIt->second, currentBody, alpha);
        }

        RenderDrawItemSnapshot item;
        item.Id = interpolated.Id;
        item.World = BuildWorldMatrix(interpolated);
        renderSnapshot.AddDrawItem(item);
    }

    return renderSnapshot;
}

CameraRenderState RenderSnapshotBuilder::BuildCamera(const CameraLogicState& cameraState) const
{
    CameraRenderState renderState;
    renderState.Position = cameraState.Position;
    renderState.Rotation = cameraState.Rotation;

    XMVECTOR position = XMLoadFloat3(&cameraState.Position);
    XMVECTOR rotation = XMLoadFloat4(&cameraState.Rotation);
    XMVECTOR forward = XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotation);
    XMVECTOR up = XMVector3Rotate(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), rotation);

    XMMATRIX view = XMMatrixLookToLH(position, forward, up);
    XMMATRIX proj = XMMatrixPerspectiveFovLH(cameraState.FovY, cameraState.AspectRatio, cameraState.NearZ, cameraState.FarZ);
    XMMATRIX viewProj = view * proj;

    XMStoreFloat4x4(&renderState.View, view);
    XMStoreFloat4x4(&renderState.Proj, proj);
    XMStoreFloat4x4(&renderState.ViewProj, viewProj);
    return renderState;
}

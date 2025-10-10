#pragma once
#include "IComponent.h"
#include <DirectXMath.h>

using namespace DirectX;

struct RigidbodyComponent : public IComponent {
    COMPONENT_TYPE(RigidbodyComponent)

        XMFLOAT3 velocity = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 acceleration = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 angularVelocity = { 0.0f, 0.0f, 0.0f };

    float mass = 1.0f;
    float drag = 0.0f;
    float angularDrag = 0.05f;
    bool useGravity = true;
    bool isKinematic = false;

    // Force accumulation
    XMFLOAT3 forceAccumulator = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 torqueAccumulator = { 0.0f, 0.0f, 0.0f };

    void AddForce(const XMFLOAT3& force) {
        XMVECTOR f = XMLoadFloat3(&forceAccumulator);
        XMVECTOR newForce = XMLoadFloat3(&force);
        XMStoreFloat3(&forceAccumulator, XMVectorAdd(f, newForce));
    }

    void ClearForces() {
        forceAccumulator = { 0.0f, 0.0f, 0.0f };
        torqueAccumulator = { 0.0f, 0.0f, 0.0f };
    }
};
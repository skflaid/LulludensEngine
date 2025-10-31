#pragma once
#include "Core/IComponent.h"
#include <DirectXMath.h>

using namespace DirectX;

struct RigidbodyComponent : public IComponent {
    COMPONENT_TYPE(RigidbodyComponent)

    XMFLOAT3 velocity = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 acceleration = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 angularVelocity = { 0.0f, 0.0f, 0.0f };

    float mass = 1.0f;
    float angularDrag = 0.05f;
    bool useGravity = true;
    bool isKinematic = false;

    float restitution = 0.3f;  // 반발 계수 (0~1)
    float friction = 0.5f;     // 마찰 계수 (0~1)
    float drag = 0.01f;        // 공기 저항
    float linearDamping = 0.05f;

    // Force accumulation
    XMFLOAT3 forceAccumulator = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 torqueAccumulator = { 0.0f, 0.0f, 0.0f };

    void AddForce(const XMFLOAT3& force) {
        XMVECTOR f = XMLoadFloat3(&forceAccumulator);
        XMVECTOR newForce = XMLoadFloat3(&force);
        XMStoreFloat3(&forceAccumulator, XMVectorAdd(f, newForce));
    }

    void AddTorque(const XMFLOAT3& torque) {
        XMVECTOR t = XMLoadFloat3(&torqueAccumulator);
        XMVECTOR newTorque = XMLoadFloat3(&torque);
        XMStoreFloat3(&torqueAccumulator, XMVectorAdd(t, newTorque));
    }

    void ClearForces() {
        forceAccumulator = { 0.0f, 0.0f, 0.0f };
        torqueAccumulator = { 0.0f, 0.0f, 0.0f };
    }
};

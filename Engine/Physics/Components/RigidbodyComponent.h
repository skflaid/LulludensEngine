#pragma once
#include "Core/IComponent.h"
#include <DirectXMath.h>

using namespace DirectX;

struct RigidbodyComponent : public IComponent {
    COMPONENT_TYPE(RigidbodyComponent)

    XMFLOAT3 velocity = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 acceleration = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 angularVelocity = { 0.0f, 0.0f, 0.0f };
    
private:
    float mass = 1.0f;
    float inverseMass = 1.0f;

public:

    float angularDrag = 0.05f;
    bool useGravity = true;
    bool isKinematic = false;

    float restitution = 0.3f;  // 반발 계수 (0~1)
    float friction = 0.5f;     // 마찰 계수 (0~1)
    float drag = 0.01f;        // 공기 저항
    float linearDamping = 0.05f;
    float inertia = 1.0f;//관성

    bool isAwake = true;      // 깨어있는지 여부
    float sleepTimer = 0.0f;  // 정지 상태 지속 시간

    // 임계값 설정 (이 값보다 속도가 낮으면 정지한 것으로 간주)
    float sleepThreshold = 0.05f;

    // Force accumulation
    XMFLOAT3 forceAccumulator = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 torqueAccumulator = { 0.0f, 0.0f, 0.0f };

public:
    void SetMass(float newMass)
    {
        if (newMass <= 0.0f)
        {
            mass = 0.0f;
            inverseMass = 0.0f;
            return;
        }

        mass = newMass;
        inverseMass = 1.0f / mass;
    }

    float GetMass() const
    {
        return mass;
    }

    float GetInverseMass() const
    {
        return inverseMass;
    }

    bool HasFiniteMass() const
    {
        return inverseMass > 0.0f;
    }

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

    // Compute proper moment of inertia for a box shape
    // For a uniform box with dimensions (w, h, d), the moments are:
    // Ix = (m/12)(h² + d²), Iy = (m/12)(w² + d²), Iz = (m/12)(w² + h²)
    // Since we use scalar inertia, we average the three principal moments
    void ComputeBoxInertia(const XMFLOAT3& size, const XMFLOAT3& scale) {
        float w = size.x * scale.x;
        float h = size.y * scale.y;
        float d = size.z * scale.z;
        
        // Principal moments of inertia
        float Ix = (mass / 12.0f) * (h*h + d*d);
        float Iy = (mass / 12.0f) * (w*w + d*d);
        float Iz = (mass / 12.0f) * (w*w + h*h);
        
        // Use average for scalar approximation
        inertia = (Ix + Iy + Iz) / 3.0f;
        
        // Safety: ensure non-zero
        if (inertia < 0.001f) inertia = 0.001f;
    }
};

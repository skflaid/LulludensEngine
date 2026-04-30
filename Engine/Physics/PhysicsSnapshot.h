#pragma once

#include "Core/EngineTypes.h"
#include "Physics/Components/ColliderComponent.h"
#include <DirectXMath.h>
#include <cstdint>
#include <vector>

class Entity;

struct PhysicsBodySnapshot {
    // Value-copy of the render-relevant physics/body state for one entity.
    EntityId Id = 0;
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT4 Rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    DirectX::XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };
    DirectX::XMFLOAT3 Velocity = { 0.0f, 0.0f, 0.0f };
    DirectX::XMFLOAT3 AngularVelocity = { 0.0f, 0.0f, 0.0f };
    ColliderType Collider = ColliderType::Box;
    bool HasRigidbody = false;
    bool HasCollider = false;
};

class PhysicsSnapshot {
public:
    // Captures live ECS components into immutable data for cross-thread handoff.
    static PhysicsSnapshot Capture(uint64_t tickIndex, double simulationTimeSeconds, const std::vector<Entity*>& entities);

    uint64_t GetTickIndex() const { return m_TickIndex; }
    double GetSimulationTimeSeconds() const { return m_SimulationTimeSeconds; }
    const std::vector<PhysicsBodySnapshot>& GetBodies() const { return m_Bodies; }
    bool IsValid() const { return m_Valid; }

private:
    uint64_t m_TickIndex = 0;
    double m_SimulationTimeSeconds = 0.0;
    std::vector<PhysicsBodySnapshot> m_Bodies;
    bool m_Valid = false;
};

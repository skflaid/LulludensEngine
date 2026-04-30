#include "PhysicsSnapshot.h"

#include "Core/Entity.h"
#include "Physics/Components/RigidbodyComponent.h"
#include "Renderer/Components/TransformComponent.h"

using namespace DirectX;

namespace {
XMFLOAT4 EulerToQuaternion(const XMFLOAT3& rotation)
{
    XMFLOAT4 result;
    XMStoreFloat4(&result, XMQuaternionRotationRollPitchYaw(rotation.x, rotation.y, rotation.z));
    return result;
}
}

PhysicsSnapshot PhysicsSnapshot::Capture(
    uint64_t tickIndex,
    double simulationTimeSeconds,
    const std::vector<Entity*>& entities)
{
    // The physics side owns this capture. Everything stored in the result is a
    // value copy, so the render side can read it without touching live ECS data.
    PhysicsSnapshot snapshot;
    snapshot.m_TickIndex = tickIndex;
    snapshot.m_SimulationTimeSeconds = simulationTimeSeconds;
    snapshot.m_Valid = true;
    snapshot.m_Bodies.reserve(entities.size());

    for (Entity* entity : entities) {
        // Skip entities that cannot contribute a transform to the render frame.
        if (!entity || !entity->IsActive()) {
            continue;
        }

        auto* transform = entity->GetComponent<TransformComponent>();
        if (!transform) {
            continue;
        }

        PhysicsBodySnapshot body;
        body.Id = entity->GetID();
        body.Position = transform->position;
        body.Rotation = EulerToQuaternion(transform->rotation);
        body.Scale = transform->scale;

        if (auto* rigidbody = entity->GetComponent<RigidbodyComponent>()) {
            body.HasRigidbody = true;
            body.Velocity = rigidbody->velocity;
            body.AngularVelocity = rigidbody->angularVelocity;
        }

        if (auto* collider = entity->GetCollider()) {
            body.HasCollider = true;
            body.Collider = collider->type;
        }

        snapshot.m_Bodies.push_back(body);
    }

    return snapshot;
}

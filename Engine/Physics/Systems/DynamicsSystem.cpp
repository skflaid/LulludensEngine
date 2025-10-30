#include "DynamicsSystem.h"
#include "../Components/RigidbodyComponent.h"
#include "../../Renderer/Components/TransformComponent.h"
#include <Windows.h>

DynamicsSystem::DynamicsSystem(PhysicsWorld* world) : m_PhysicsWorld(world) {}

void DynamicsSystem::Initialize() {}
void DynamicsSystem::Shutdown() {}

void DynamicsSystem::RegisterEntity(Entity* entity) {
    if (entity->HasComponent<RigidbodyComponent>() && entity->HasComponent<TransformComponent>()) {
        m_Entities.push_back(entity);
        // 디버그용
        OutputDebugStringA("DynamicsSystem: Entity registered\n");
    }
}

void DynamicsSystem::UnregisterEntity(Entity* entity) {
    m_Entities.erase(std::remove(m_Entities.begin(), m_Entities.end(), entity), m_Entities.end());
}

void DynamicsSystem::Update(float deltaTime) {
    if (!IsEnabled()) return;

    for (auto* entity : m_Entities) {
        if (!entity->IsActive()) continue;

        ApplyGravity(entity, deltaTime);
        ApplyForces(entity, deltaTime);
        IntegrateVelocity(entity, deltaTime);
        IntegratePosition(entity, deltaTime);

        auto* rb = entity->GetComponent<RigidbodyComponent>();
        rb->ClearForces();
    }
}

void DynamicsSystem::ApplyGravity(Entity* entity, float deltaTime) {
    auto* rb = entity->GetComponent<RigidbodyComponent>();
    if (rb->useGravity && !rb->isKinematic) {
        rb->AddForce(m_PhysicsWorld->GetGravity());
    }
}

void DynamicsSystem::ApplyForces(Entity* entity, float deltaTime) {
    // This is where you would apply other forces like drag, etc.
}

void DynamicsSystem::IntegrateVelocity(Entity* entity, float deltaTime) {
    auto* rb = entity->GetComponent<RigidbodyComponent>();
    if (rb->isKinematic) return;

    // a = F / m
    XMVECTOR acc = XMLoadFloat3(&rb->forceAccumulator);
    acc = XMVectorScale(acc, 1.0f / rb->mass);
    XMStoreFloat3(&rb->acceleration, acc);

    // v = v0 + a*t
    XMVECTOR vel = XMLoadFloat3(&rb->velocity);
    vel = XMVectorAdd(vel, XMVectorScale(acc, deltaTime));
    XMStoreFloat3(&rb->velocity, vel);
}

void DynamicsSystem::IntegratePosition(Entity* entity, float deltaTime) {
    auto* rb = entity->GetComponent<RigidbodyComponent>();
    auto* transform = entity->GetComponent<TransformComponent>();
    if (rb->isKinematic) return;

    // p = p0 + v*t
    XMVECTOR pos = XMLoadFloat3(&transform->position);
    XMVECTOR vel = XMLoadFloat3(&rb->velocity);
    pos = XMVectorAdd(pos, XMVectorScale(vel, deltaTime));
    XMStoreFloat3(&transform->position, pos);
}

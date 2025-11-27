#include "DynamicsSystem.h"
#include "Core/Entity.h"
#include "../Components/RigidbodyComponent.h"
#include "../Components/ColliderComponent.h"
#include "../../Renderer/Components/TransformComponent.h"
#include <Windows.h>
#include <algorithm>
#include <DirectXMath.h>

using namespace DirectX;

DynamicsSystem::DynamicsSystem(PhysicsWorld* world) : m_PhysicsWorld(world) {}

void DynamicsSystem::Initialize() {}

void DynamicsSystem::Shutdown() {}

void DynamicsSystem::RegisterEntity(Entity* entity) {
    if (entity->HasComponent<RigidbodyComponent>() && entity->HasComponent<TransformComponent>()) {
        m_Entities.push_back(entity);
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

        auto* rb = entity->GetComponent<RigidbodyComponent>();
        if (!rb) continue;

        // 2. 중력 적용 (단순히 아래로 향하는 힘을 더함)
        ApplyGravity(entity, deltaTime);

        // 3. 다른 모든 힘(수직 항력, 마찰력 등)을 계산하여 중력을 조정
        ApplyForces(entity, deltaTime);

        // 4. 최종 힘을 바탕으로 속도와 위치 계산
        IntegrateVelocity(entity, deltaTime);
        IntegratePosition(entity, deltaTime);

        //5. 회전 계산
        IntegrateAngularVelocity(entity, deltaTime);
        IntegrateRotation(entity, deltaTime);

        // 1. 힘 누적기 초기화 (다음 프레임을 위해 마지막에 초기화)
        rb->ClearForces();
    }
}

void DynamicsSystem::ApplyGravity(Entity* entity, float deltaTime) {
    auto* rb = entity->GetComponent<RigidbodyComponent>();
    // useGravity가 true이면 일단 중력을 더합니다.
    if (rb->useGravity && !rb->isKinematic) {
        rb->AddForce(m_PhysicsWorld->GetGravity());
    }
}


void DynamicsSystem::ApplyForces(Entity* entity, float deltaTime) {
    auto* rb = entity->GetComponent<RigidbodyComponent>();
    if (rb->isKinematic) return;

    auto* collider = entity->GetCollider();

    // 물체가 어딘가에 접촉하고 있을 때만 작동
    if (collider && collider->hasContact) {
        // 현재까지 누적된 힘 (지금은 중력만 들어있음)
        XMVECTOR totalForce = XMLoadFloat3(&rb->forceAccumulator);

        // 접촉면의 법선 벡터
        XMVECTOR normal = XMLoadFloat3(&collider->contactNormal);
        normal = XMVector3Normalize(normal);

        // 1. 수직 항력 계산: 현재 힘(중력)에서 법선 방향 성분 제거
        XMVECTOR normalForceComponent = XMVector3Dot(totalForce, normal) * normal;
        totalForce -= normalForceComponent;

        // --- 2. 마찰력 추가 (이 부분이 핵심) ---
        XMVECTOR velocity = XMLoadFloat3(&rb->velocity);

        // 속도가 거의 0이 아니면 마찰력 계산
        if (XMVectorGetX(XMVector3LengthSq(velocity)) > 1e-6f) {
            // 미끄러지는 속도의 반대 방향 벡터
            XMVECTOR frictionDir = -XMVector3Normalize(velocity);

            // 마찰력의 크기는 보통 마찰 계수와 수직 항력의 크기에 비례합니다.
            // 여기서는 간단하게 마찰 계수만큼의 힘을 적용합니다.
            float frictionMagnitude = rb->friction; // RigidbodyComponent에 friction 변수 추가
            XMVECTOR frictionForce = frictionDir * frictionMagnitude;

            // 최종 힘에 마찰력을 더해줍니다 (실제로는 빼는 효과)
            totalForce += frictionForce;
        }

        // 3. 조정된 최종 힘을 다시 forceAccumulator에 저장
        XMStoreFloat3(&rb->forceAccumulator, totalForce);
    }
}

void DynamicsSystem::ApplySlopeForce(Entity* entity, const XMFLOAT3& contactNormal) {
    auto* rb = entity->GetComponent<RigidbodyComponent>();

    XMFLOAT3 gravity = m_PhysicsWorld->GetGravity();
    XMVECTOR gravVec = XMLoadFloat3(&gravity);
    XMVECTOR normal = XMLoadFloat3(&contactNormal);

    normal = XMVector3Normalize(normal);

    XMVECTOR dotProduct = XMVector3Dot(gravVec, normal);
    XMVECTOR normalComponent = normal * dotProduct;
    XMVECTOR slideForce = gravVec - normalComponent;

    XMFLOAT3 slideForcef;
    XMStoreFloat3(&slideForcef, slideForce);

    rb->AddForce(slideForcef);
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
    vel *= (1.0f - rb->linearDamping);
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

void DynamicsSystem::IntegrateAngularVelocity(Entity* entity, float deltaTime) {
    auto* rb = entity->GetComponent<RigidbodyComponent>();
    if (rb->isKinematic) return;

    // τ -> α = τ / I
    XMVECTOR torque = XMLoadFloat3(&rb->torqueAccumulator);

    // 관성 0 방지
    float inertia = (rb->inertia != 0.0f) ? rb->inertia : 1.0f;
    XMVECTOR angularAcc = XMVectorScale(torque, 1.0f / inertia);

    // ω = ω0 + α * dt
    XMVECTOR angVel = XMLoadFloat3(&rb->angularVelocity);
    angVel = XMVectorAdd(angVel, XMVectorScale(angularAcc, deltaTime));

    // 각 저항(감쇠) 적용
    float damping = 1.0f - rb->angularDrag;
    if (damping < 0.0f) damping = 0.0f;
    if (damping > 1.0f) damping = 1.0f;
    angVel = XMVectorScale(angVel, damping);

    XMStoreFloat3(&rb->angularVelocity, angVel);

    XMFLOAT3 w;
    XMStoreFloat3(&w, angVel);
    char buf[128];
    sprintf_s(buf, "w: %.3f, %.3f, %.3f\n", w.x, w.y, w.z);
    OutputDebugStringA(buf);
}

void DynamicsSystem::IntegrateRotation(Entity* entity, float deltaTime) {
    auto* rb = entity->GetComponent<RigidbodyComponent>();
    auto* transform = entity->GetComponent<TransformComponent>();
    if (rb->isKinematic || !transform) return;

    // 현재 각속도(라디안/초)
    XMVECTOR angVel = XMLoadFloat3(&rb->angularVelocity);

    // Δrotation = ω * dt
    XMVECTOR deltaRot = XMVectorScale(angVel, deltaTime);

    XMVECTOR rot = XMLoadFloat3(&transform->rotation);
    rot = XMVectorAdd(rot, deltaRot);

    XMStoreFloat3(&transform->rotation, rot);
}
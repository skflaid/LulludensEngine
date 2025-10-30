#include "CollisionSystem.h"
#include "../Components/ColliderComponent.h"
#include "../../Renderer/Components/TransformComponent.h"
#include "../Components/RigidbodyComponent.h" 
#include <Windows.h>

CollisionSystem::CollisionSystem(PhysicsWorld* world) : m_PhysicsWorld(world) {}

void CollisionSystem::Initialize() {}
void CollisionSystem::Shutdown() {}

void CollisionSystem::RegisterEntity(Entity* entity) {
    // ColliderComponent이거나 그 파생 클래스(BoxCollider 등)인 경우
    if (entity->HasComponent<ColliderComponent>() ||
        entity->HasComponent<BoxCollider>() ||
        entity->HasComponent<SphereCollider>() /* 필요시 추가 */) {
        m_Entities.push_back(entity);
        char buf[128];
        sprintf_s(buf, "CollisionSystem::RegisterEntity: m_Entities.size()=%zu\n", m_Entities.size());
        OutputDebugStringA(buf);
    }
}

void CollisionSystem::UnregisterEntity(Entity* entity) {
    m_Entities.erase(std::remove(m_Entities.begin(), m_Entities.end(), entity), m_Entities.end());
}

void CollisionSystem::Update(float deltaTime) {
    if (!IsEnabled()) return;

    BroadPhaseDetection();

    // 디버그 출력
    char buffer[256];
    sprintf_s(buffer, "CollisionSystem: Found %zu collision pairs\n", m_CollisionPairs.size());
    OutputDebugStringA(buffer);

    NarrowPhaseDetection();
    ResolveCollisions();
}

void CollisionSystem::BroadPhaseDetection() {
    //디버그
    char buf[128];
    sprintf_s(buf, "BroadPhaseDetection: registered entities=%zu\n", m_Entities.size());
    OutputDebugStringA(buf);

    // Simple N-squared broadphase for now
    m_CollisionPairs.clear();
    for (size_t i = 0; i < m_Entities.size(); ++i) {
        for (size_t j = i + 1; j < m_Entities.size(); ++j) {
            // In a real engine, you'd use a spatial partitioning structure like a grid or BVH
            // For now, we assume all pairs might collide.
            if (CheckCollision(m_Entities[i], m_Entities[j])) {
                m_CollisionPairs.push_back({ m_Entities[i], m_Entities[j] });
            }
        }
    }
}

bool CollisionSystem::CheckCollision(Entity* entityA, Entity* entityB) {
    auto* transA = entityA->GetComponent<TransformComponent>();
    auto* collA = entityA->GetComponent<BoxCollider>();
    auto* transB = entityB->GetComponent<TransformComponent>();
    auto* collB = entityB->GetComponent<BoxCollider>();

    if (!transA || !collA || !transB || !collB) return false;

    // 3D AABB 충돌 검사
    float a_min_x = transA->position.x - (collA->size.x * transA->scale.x) / 2.0f;
    float a_max_x = transA->position.x + (collA->size.x * transA->scale.x) / 2.0f;
    float a_min_y = transA->position.y - (collA->size.y * transA->scale.y) / 2.0f;
    float a_max_y = transA->position.y + (collA->size.y * transA->scale.y) / 2.0f;
    float a_min_z = transA->position.z - (collA->size.z * transA->scale.z) / 2.0f;
    float a_max_z = transA->position.z + (collA->size.z * transA->scale.z) / 2.0f;

    float b_min_x = transB->position.x - (collB->size.x * transB->scale.x) / 2.0f;
    float b_max_x = transB->position.x + (collB->size.x * transB->scale.x) / 2.0f;
    float b_min_y = transB->position.y - (collB->size.y * transB->scale.y) / 2.0f;
    float b_max_y = transB->position.y + (collB->size.y * transB->scale.y) / 2.0f;
    float b_min_z = transB->position.z - (collB->size.z * transB->scale.z) / 2.0f;
    float b_max_z = transB->position.z + (collB->size.z * transB->scale.z) / 2.0f;

    // 3축 모두에서 겹침이 있어야 충돌
    bool collisionX = a_max_x >= b_min_x && a_min_x <= b_max_x;
    bool collisionY = a_max_y >= b_min_y && a_min_y <= b_max_y;
    bool collisionZ = a_max_z >= b_min_z && a_min_z <= b_max_z;

    return collisionX && collisionY && collisionZ;
}

void CollisionSystem::NarrowPhaseDetection() {
    // More detailed collision info would be generated here
}

void CollisionSystem::ResolveCollisions() {
    for (const auto& pair : m_CollisionPairs) {
        //debug
        OutputDebugStringA("CollisionSystem: Resolving collision!\n");
        auto* transA = pair.entityA->GetComponent<TransformComponent>();
        auto* transB = pair.entityB->GetComponent<TransformComponent>();
        auto* rbA = pair.entityA->GetComponent<RigidbodyComponent>();
        auto* rbB = pair.entityB->GetComponent<RigidbodyComponent>();
        auto* collA = pair.entityA->GetComponent<BoxCollider>();
        auto* collB = pair.entityB->GetComponent<BoxCollider>();

        if (!transA || !transB || !collA || !collB) continue;

        // 충돌 깊이 계산
        float a_half_x = (collA->size.x * transA->scale.x) / 2.0f;
        float a_half_y = (collA->size.y * transA->scale.y) / 2.0f;
        float a_half_z = (collA->size.z * transA->scale.z) / 2.0f;

        float b_half_x = (collB->size.x * transB->scale.x) / 2.0f;
        float b_half_y = (collB->size.y * transB->scale.y) / 2.0f;
        float b_half_z = (collB->size.z * transB->scale.z) / 2.0f;

        float dx = transA->position.x - transB->position.x;
        float dy = transA->position.y - transB->position.y;
        float dz = transA->position.z - transB->position.z;

        float overlap_x = (a_half_x + b_half_x) - abs(dx);
        float overlap_y = (a_half_y + b_half_y) - abs(dy);
        float overlap_z = (a_half_z + b_half_z) - abs(dz);

        // 가장 작은 겹침을 찾아서 그 축으로 밀어냄
        if (overlap_x < overlap_y && overlap_x < overlap_z) {
            // X축으로 분리
            float direction = (dx > 0) ? 1.0f : -1.0f;
            if (rbA && !rbA->isKinematic) {
                transA->position.x += direction * overlap_x * 0.5f;
                rbA->velocity.x = 0.0f;
            }
            if (rbB && !rbB->isKinematic) {
                transB->position.x -= direction * overlap_x * 0.5f;
                rbB->velocity.x = 0.0f;
            }
        }
        else if (overlap_y < overlap_z) {
            // Y축으로 분리 (가장 흔한 경우: 바닥 충돌)
            float direction = (dy > 0) ? 1.0f : -1.0f;
            if (rbA && !rbA->isKinematic) {
                transA->position.y += direction * overlap_y * 0.5f;
                rbA->velocity.y = 0.0f;
            }
            if (rbB && !rbB->isKinematic) {
                transB->position.y -= direction * overlap_y * 0.5f;
                rbB->velocity.y = 0.0f;
            }
        }
        else {
            // Z축으로 분리
            float direction = (dz > 0) ? 1.0f : -1.0f;
            if (rbA && !rbA->isKinematic) {
                transA->position.z += direction * overlap_z * 0.5f;
                rbA->velocity.z = 0.0f;
            }
            if (rbB && !rbB->isKinematic) {
                transB->position.z -= direction * overlap_z * 0.5f;
                rbB->velocity.z = 0.0f;
            }
        }
    }
}

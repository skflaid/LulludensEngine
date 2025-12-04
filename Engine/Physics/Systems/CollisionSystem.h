#pragma once
#include "Core/ISystem.h"
#include "Core/Entity.h"
#include "Physics/PhysicsWorld.h"
#include "Components/RigidbodyComponent.h"
#include <vector>

struct CollisionPair {
    Entity* entityA;
    Entity* entityB;
};

class CollisionSystem : public ISystem {
public:
    CollisionSystem(PhysicsWorld* world);

    void Initialize() override;
    void Update(float deltaTime) override;
    void Shutdown() override;

    const char* GetName() const override { return "CollisionSystem"; }

    void RegisterEntity(Entity* entity);
    void UnregisterEntity(Entity* entity);

private:
    struct CollisionPair {
        Entity* entityA;
        Entity* entityB;
    };

    void BroadPhaseDetection();
    bool CheckAABBCollision(Entity* entityA, Entity* entityB);
    void NarrowPhaseDetection();
    void ResolveCollisions();

    // === 충돌 타입별 검사 함수 선언 ===
    bool TestBoxBox(Entity* entityA, Entity* entityB);
    bool TestSphereSphere(Entity* entityA, Entity* entityB);
    bool TestBoxSphere(Entity* boxEntity, Entity* sphereEntity);
    bool TestMeshSphere(Entity* meshEntity, Entity* sphereEntity);
    bool TestMeshBox(Entity* meshEntity, Entity* boxEntity);
    bool m_Enabled = true;

private:
    PhysicsWorld* m_PhysicsWorld;
    std::vector<Entity*> m_Entities;
    std::vector<CollisionPair> m_CollisionPairs;
};

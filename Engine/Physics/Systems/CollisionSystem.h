#pragma once
#include "Core/ISystem.h"
#include "Core/Entity.h"
#include "Physics/PhysicsWorld.h"
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
    void BroadPhaseDetection();
    void NarrowPhaseDetection();
    void ResolveCollisions();

    bool CheckCollision(Entity* entityA, Entity* entityB);

private:
    PhysicsWorld* m_PhysicsWorld;
    std::vector<Entity*> m_Entities;
    std::vector<CollisionPair> m_CollisionPairs;
};

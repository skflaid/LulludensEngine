#pragma once
#include "ISystem.h"
#include "PhysicsWorld.h"
#include <vector>

struct CollisionPair {
    Entity* entityA;
    Entity* entityB;
    // Additional collision data...
};

class CollisionSystem : public ISystem {
public:
    CollisionSystem(PhysicsWorld* world) : m_PhysicsWorld(world) {}

    void Initialize() override {}
    void Update(float deltaTime) override;
    void Shutdown() override {}

    const char* GetName() const override { return "CollisionSystem"; }

private:
    PhysicsWorld* m_PhysicsWorld;
    std::vector<CollisionPair> m_CollisionPairs;

    // Broad phase collision detection
    void BroadPhaseDetection();

    // Narrow phase collision detection
    void NarrowPhaseDetection();

    // Collision resolution
    void ResolveCollisions();
};
// Physics/PhysicsWorld.cpp

#include "PhysicsWorld.h"
#include "Systems/DynamicsSystem.h"
#include "Systems/CollisionSystem.h"

// --- 생성자에서 중력과 고정 시간 간격을 초기화합니다 ---
PhysicsWorld::PhysicsWorld()
    : m_Gravity({ 0.0f, -9.81f, 0.0f }), m_FixedTimeStep(1.0f / 60.0f)
{
}
// --- 여기까지가 핵심 수정사항입니다 ---

PhysicsWorld::~PhysicsWorld() {
    Shutdown();
}

void PhysicsWorld::Initialize() {
    // 현재는 특별한 초기화 내용 없음
}

void PhysicsWorld::Update(float deltaTime) {
    // 시스템들이 추가된 순서대로 실행됨
    // MainApp.cpp에서 Dynamics -> Collision 순서로 AddSystem을 호출하는 것이 중요
    for (auto& system : m_Systems) {
        if (system->IsEnabled()) {
            system->Update(deltaTime);
        }
    }
}

void PhysicsWorld::Shutdown() {
    for (auto& system : m_Systems) {
        system->Shutdown();
    }
    m_Systems.clear();
    m_Entities.clear();
}

void PhysicsWorld::RegisterEntity(Entity* entity) {
    m_Entities.push_back(entity);

    // 모든 시스템에 엔티티 등록 시도
    for (auto& system : m_Systems) {
        // DynamicsSystem인지 확인
        DynamicsSystem* dynamicsSystem = dynamic_cast<DynamicsSystem*>(system.get());
        if (dynamicsSystem) {
            dynamicsSystem->RegisterEntity(entity);
            continue;
        }

        // CollisionSystem인지 확인
        CollisionSystem* collisionSystem = dynamic_cast<CollisionSystem*>(system.get());
        if (collisionSystem) {
            collisionSystem->RegisterEntity(entity);
            continue;
        }
    }
}

void PhysicsWorld::UnregisterEntity(Entity* entity) {
    // PhysicsWorld의 엔티티 목록에서 제거
    m_Entities.erase(std::remove(m_Entities.begin(), m_Entities.end(), entity), m_Entities.end());

    // 모든 시스템에 엔티티 등록 해제 요청
    for (auto& system : m_Systems) {
        if (dynamic_cast<DynamicsSystem*>(system.get())) {
            dynamic_cast<DynamicsSystem*>(system.get())->UnregisterEntity(entity);
        }
        if (dynamic_cast<CollisionSystem*>(system.get())) {
            dynamic_cast<CollisionSystem*>(system.get())->UnregisterEntity(entity);
        }
    }
}

void PhysicsWorld::AddSystem(std::unique_ptr<ISystem> system) {
    m_Systems.push_back(std::move(system));
}

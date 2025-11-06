#include "GameEngine.h"
#include "Core/Entity.h"
#include "Physics/PhysicsWorld.h"
#include "Physics/PhysicsScheduler.h"
#include "Physics/Systems/DynamicsSystem.h"
#include "Physics/Systems/CollisionSystem.h"
#include "Physics/Components/RigidbodyComponent.h"
#include "Physics/Components/ColliderComponent.h"
#include "Renderer/Systems/RenderSystem.h"
#include "Renderer/Components/TransformComponent.h"
#include "Renderer/Components/MeshComponent.h"
#include "Renderer/Components/MaterialComponent.h"

GameEngine::GameEngine() = default;
GameEngine::~GameEngine() = default;
GameEngine::GameEngine(GameEngine&&) noexcept = default;
GameEngine& GameEngine::operator=(GameEngine&&) noexcept = default;

bool GameEngine::Initialize(HWND hwnd, uint32_t width, uint32_t height)
{
    m_PhysicsWorld = std::make_shared<PhysicsWorld>();
    m_PhysicsWorld->Initialize();

    auto dynamicsSystem = std::make_unique<DynamicsSystem>(m_PhysicsWorld.get());
    dynamicsSystem->Initialize();
    m_PhysicsWorld->AddSystem(std::move(dynamicsSystem));

    auto collisionSystem = std::make_unique<CollisionSystem>(m_PhysicsWorld.get());
    collisionSystem->Initialize();
    m_PhysicsWorld->AddSystem(std::move(collisionSystem));

    m_PhysicsScheduler = std::make_unique<PhysicsScheduler>(m_PhysicsWorld);
    m_RenderSystem = std::make_unique<RenderSystem>(hwnd, width, height);
    m_RenderSystem->Initialize();

    CreateEntities();
    return true;
}

void GameEngine::Update(float deltaTime)
{
    if (m_PhysicsScheduler) m_PhysicsScheduler->Update(deltaTime);
    if (m_RenderSystem)     m_RenderSystem->Update(deltaTime);
}

void GameEngine::Render()
{
    if (m_RenderSystem) m_RenderSystem->Render();
}

void GameEngine::Shutdown()
{
    if (m_RenderSystem) { m_RenderSystem->Shutdown(); m_RenderSystem.reset(); }
    if (m_PhysicsWorld) { m_PhysicsWorld->Shutdown(); m_PhysicsWorld.reset(); }
    m_Entities.clear();
}

uint64_t GameEngine::GetEntityCount() const
{
    return static_cast<uint64_t>(m_Entities.size());
}

Entity* GameEngine::GetEntityByIndex(uint64_t index)
{
    if (index >= m_Entities.size()) return nullptr;
    return m_Entities[index].get();
}

void GameEngine::CreateEntities()
{
    // Cube
    auto cube = std::make_unique<Entity>(1);
    auto t = cube->AddComponent<TransformComponent>();
    t->SetPosition(0.0f, 5.0f, 0.0f);
    t->SetScale(1.0f, 1.0f, 1.0f);
    cube->AddComponent<RigidbodyComponent>()->mass = 1.0f;
    cube->AddComponent<BoxCollider>()->size = { 1.0f, 1.0f, 1.0f };
    cube->AddComponent<MeshComponent>()->CreateCube();
    cube->AddComponent<MaterialComponent>()->SetAlbedo(0.8f, 0.3f, 0.3f);

    m_PhysicsWorld->RegisterEntity(cube.get());
    m_RenderSystem->RegisterEntity(cube.get());
    m_Entities.push_back(std::move(cube));

    // Ground
    auto ground = std::make_unique<Entity>(2);
    auto gt = ground->AddComponent<TransformComponent>();
    gt->SetPosition(0.0f, 0.0f, 0.0f);
    gt->SetScale(10.0f, 0.1f, 10.0f);
    gt->SetRotation(0.0f, 0.0f, 0.0f);
    auto rb = ground->AddComponent<RigidbodyComponent>();
    rb->isKinematic = true;
    ground->AddComponent<BoxCollider>()->size = { 1.0f, 1.0f, 1.0f };
    ground->AddComponent<MeshComponent>()->CreateCube();
    ground->AddComponent<MaterialComponent>()->SetAlbedo(0.3f, 0.8f, 0.3f);

    m_PhysicsWorld->RegisterEntity(ground.get());
    m_RenderSystem->RegisterEntity(ground.get());
    m_Entities.push_back(std::move(ground));
}

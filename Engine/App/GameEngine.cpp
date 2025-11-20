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
#include "Core/FBXLoader.h"
#include "Core/ModelInstantiation.h"
#include "Core/InputManager.h" 
#include "Renderer/Systems/CameraSystem.h" 
#include "Renderer/Components/CameraComponent.h" 
// Model.h는 FBXLoader.h에서 이미 포함되므로 여기서는 제거

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
    m_RenderSystem = std::make_unique<RenderSystem>(this, hwnd, width, height);
    m_RenderSystem->Initialize();

    // CameraSystem 생성 및 초기화
    m_CameraSystem = std::make_unique<CameraSystem>();
    m_CameraSystem->Initialize(); 
    m_CameraSystem->SetGameEngine(this); 

    CreateEntities();
    return true;
}

void GameEngine::Update(float deltaTime)
{
    InputManager::Get()->Update();

    if (m_CameraSystem) m_CameraSystem->Update(deltaTime);
    if (m_PhysicsScheduler) m_PhysicsScheduler->Update(deltaTime);
    if (m_RenderSystem)     m_RenderSystem->Update(deltaTime);
}

void GameEngine::Render()
{
    if (m_RenderSystem) m_RenderSystem->Render();
}

void GameEngine::ToggleRenderMode()
{
    if (m_RenderSystem) m_RenderSystem->ToggleRenderMode();
}

void GameEngine::Shutdown()
{
    if (m_RenderSystem) { m_RenderSystem->Shutdown(); m_RenderSystem.reset(); }
    if (m_PhysicsWorld) { m_PhysicsWorld->Shutdown(); m_PhysicsWorld.reset(); }
    m_Entities.clear();
}

//카메라 관련 함수
void GameEngine::SetMainCamera(Entity* camera) { m_MainCamera = camera; }
Entity* GameEngine::GetMainCamera() const { return m_MainCamera; }

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
    // ======== 메인 카메라 생성 ========
    auto cameraEntity = std::make_unique<Entity>(4); // ID 4번으로 카메라 생성
    cameraEntity->AddComponent<TransformComponent>();
    auto cameraComp = cameraEntity->AddComponent<CameraComponent>();

    // RenderSystem으로부터 화면 너비와 높이를 가져옵니다.
    float width = static_cast<float>(m_RenderSystem->GetWidth());
    float height = static_cast<float>(m_RenderSystem->GetHeight());

    // 투영 행렬을 생성합니다.
    XMMATRIX P = XMMatrixPerspectiveFovLH(XM_PIDIV4, width / height, 0.1f, 1000.0f);
    XMStoreFloat4x4(&cameraComp->ProjMatrix, P);

    SetMainCamera(cameraEntity.get()); // 메인 카메라로 등록
    m_Entities.push_back(std::move(cameraEntity));

    // Cube
    for (int i = 0; i < 5; i++) {
        auto cube = std::make_unique<Entity>(5+i);
        auto t = cube->AddComponent<TransformComponent>();
        t->SetPosition(0.0f, 20.0f, 0.0f);
        t->SetScale(1.0f, 1.0f, 1.0f);
        cube->AddComponent<RigidbodyComponent>()->mass = 1.0f;
        cube->AddComponent<BoxCollider>()->size = { 1.0f, 1.0f, 1.0f };
        cube->AddComponent<MeshComponent>()->CreateCube();
        cube->AddComponent<MaterialComponent>()->SetAlbedo(0.8f, 0.3f, 0.3f);

        m_PhysicsWorld->RegisterEntity(cube.get());
        m_RenderSystem->RegisterEntity(cube.get());
        m_Entities.push_back(std::move(cube));
    }
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

    // FBX 모델 로드 예제
    // 주의: 실제 FBX 파일 경로로 변경해야 합니다.
    LoadFBXModel("Models/Nissan 180SX S13 (1992).fbx", 3, 90.0f, 45.0f, 0.0f, true);
}

void GameEngine::LoadFBXModel(const std::string& filePath, uint32_t entityId, float pitch, float yaw, float roll, bool attachMeshCollider)
{
    // 1. FBXLoader를 사용하여 FBX 파일 로드
    FBXLoader loader;
    Model* model = loader.Load(filePath);
    
    if (!model) {
        // 로드 실패 처리 (로그 출력 등)
        // Log::Error("Failed to load FBX file: %s", filePath.c_str());
        return;
    }

    // 2. Entity 생성 및 TransformComponent 추가
    auto fbxEntity = std::make_unique<Entity>(entityId);
    auto transform = fbxEntity->AddComponent<TransformComponent>();
    transform->SetPosition(0.0f, 2.0f, 0.0f);  // 위치 설정
    
    // 회전 설정 방법 1: 각도(degree) 단위로 설정 (추천)
    transform->SetRotationDegrees(pitch, yaw, roll);   // Y축으로 45도 회전 (테스트용)
    // transform->SetRotationDegrees(0.0f, 0.0f, 0.0f);   // 회전 없음
    
    // 회전 설정 방법 2: 라디안 단위로 직접 설정
    // transform->SetRotation(0.0f, XM_PI / 4.0f, 0.0f);   // Y축으로 45도 회전
    // Pitch (X축), Yaw (Y축), Roll (Z축)
    
    transform->SetScale(1.0f, 1.0f, 1.0f);     // 스케일 설정

    // 3. Model을 Entity로 변환 (모든 메시를 하나로 병합)
    if (ModelInstantiation::InstantiateToEntity(model, fbxEntity.get(), true)) {
        // 4. RenderSystem에 등록 (GPU 버퍼 자동 생성)
        m_RenderSystem->RegisterEntity(fbxEntity.get());
        
        // 5. MeshCollider 추가 (옵션)
        if (attachMeshCollider) {
            auto* meshComp = fbxEntity->GetComponent<MeshComponent>();
            if (meshComp) {
                auto* collider = fbxEntity->AddComponent<MeshCollider>();
                collider->meshComponent = meshComp;
                m_PhysicsWorld->RegisterEntity(fbxEntity.get());
            }
        }

        // 6. Entity를 엔진에 추가
        m_Entities.push_back(std::move(fbxEntity));
    }
    else {
        // 변환 실패 처리
        // Log::Error("Failed to instantiate model to entity");
    }

    // 7. Model 메모리 해제 (FBXLoader가 new로 할당했으므로 delete 필요)
    delete model;
}

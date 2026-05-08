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
#include "Renderer/Components/SkyComponent.h"
#include "Renderer/Systems/AnimationSystem.h"
#include "Renderer/Components/SkeletonComponent.h"
#include "Renderer/Components/SkeletalAnimationComponent.h"
#include "Threading/SnapshotBuffer.h"
#include "Threading/ThreadManager.h"

#include "Renderer/Components/TransformAnimationComponent.h"
#include <chrono>
#include <exception>
#include <thread>
// Model.h는 FBXLoader.h에서 이미 포함되므로 여기서는 제거

GameEngine::GameEngine() = default;
GameEngine::~GameEngine() = default;

bool GameEngine::Initialize(HWND hwnd, uint32_t width, uint32_t height)
{
    try {
    m_PhysicsWorld = std::make_shared<PhysicsWorld>();
    m_PhysicsWorld->Initialize();
    // Shared handoff point from physics simulation to rendering.
    m_PhysicsSnapshotBuffer = std::make_shared<SnapshotBuffer>();

    auto dynamicsSystem = std::make_unique<DynamicsSystem>(m_PhysicsWorld.get());
    dynamicsSystem->Initialize();
    m_PhysicsWorld->AddSystem(std::move(dynamicsSystem));

    auto collisionSystem = std::make_unique<CollisionSystem>(m_PhysicsWorld.get());
    collisionSystem->Initialize();
    m_PhysicsWorld->AddSystem(std::move(collisionSystem));

    m_PhysicsScheduler = std::make_unique<PhysicsScheduler>(m_PhysicsWorld);
    m_PhysicsScheduler->SetSnapshotBuffer(m_PhysicsSnapshotBuffer);

    m_RenderSystem = std::make_unique<RenderSystem>(this, hwnd, width, height);
    m_RenderSystem->Initialize();

    // CameraSystem 생성 및 초기화
    m_CameraSystem = std::make_unique<CameraSystem>();
    m_CameraSystem->Initialize(); 
    m_CameraSystem->SetGameEngine(this); 

    // AnimationSystem 생성 및 초기화
    m_AnimationSystem = std::make_unique<AnimationSystem>(this);
    m_AnimationSystem->Initialize();

    CreateEntities();
    // After all systems and initial entities exist, start the worker loops.
    StartRuntimeThreads();
    return true;
    }
    catch (const std::exception& e) {
        MessageBoxA(hwnd, e.what(), "Lulludens Engine initialization failed", MB_OK | MB_ICONERROR);
        Shutdown();
        return false;
    }
}

void GameEngine::Update(float deltaTime)
{
    InputManager::Get()->Update();
    // The game/update phase remains driven by the platform main loop.
    // It captures camera/animation state before workers consume it.
    UpdateGameThread(deltaTime);

    if (!IsThreadedRuntimeActive()) {
        // Single-thread fallback: run physics and render update in order under
        // the same lock used by the threaded path.
        std::lock_guard<std::mutex> lock(m_StateMutex);
        if (m_PhysicsScheduler) {
            const uint32_t fixedSteps = m_PhysicsScheduler->Update(deltaTime);
            m_PhysicsTickCounter.fetch_add(fixedSteps, std::memory_order_relaxed);
        }
        if (m_RenderSystem)     m_RenderSystem->Update(deltaTime);
    }
}

void GameEngine::Render()
{
    if (IsThreadedRuntimeActive()) {
        // In threaded mode RunRenderThread owns rendering, so the main loop does
        // not submit a second frame.
        return;
    }

    if (m_RenderSystem) {
        m_RenderSystem->Render();
        m_RenderFrameCounter.fetch_add(1, std::memory_order_relaxed);
    }
}

void GameEngine::ToggleRenderMode()
{
    if (m_RenderSystem) m_RenderSystem->ToggleRenderMode();
}

void GameEngine::ToggleStyleTransfer()
{
    if (m_RenderSystem) m_RenderSystem->ToggleStyleTransfer();
}

void GameEngine::Shutdown()
{
    // Join workers before destroying systems they may still be reading.
    StopRuntimeThreads();

    if (m_RenderSystem) { m_RenderSystem->Shutdown(); m_RenderSystem.reset(); }
    if (m_PhysicsSnapshotBuffer) { m_PhysicsSnapshotBuffer->Clear(); m_PhysicsSnapshotBuffer.reset(); }
    if (m_PhysicsWorld) { m_PhysicsWorld->Shutdown(); m_PhysicsWorld.reset(); }
    if (m_AnimationSystem) { m_AnimationSystem.reset(); }
    m_Entities.clear();
}

//카메라 관련 함수
void GameEngine::SetMainCamera(Entity* camera) { m_MainCamera = camera; }
Entity* GameEngine::GetMainCamera() const { return m_MainCamera; }

float GameEngine::GetPhysicsInterpolationAlpha() const
{
    // Render uses this value to blend between the last two physics snapshots.
    std::lock_guard<std::mutex> lock(m_StateMutex);
    return m_PhysicsScheduler ? m_PhysicsScheduler->GetInterpolationAlpha() : 0.0f;
}

std::optional<CameraLogicState> GameEngine::GetCameraLogicState() const
{
    // Camera state is copied out so render does not need to chase live ECS
    // pointers while the game thread may update them.
    std::lock_guard<std::mutex> lock(m_StateMutex);
    return m_CameraLogicState;
}

uint64_t GameEngine::GetGameFrameCount() const
{
    return m_GameFrameCounter.load(std::memory_order_relaxed);
}

uint64_t GameEngine::GetRenderFrameCount() const
{
    return m_RenderFrameCounter.load(std::memory_order_relaxed);
}

uint64_t GameEngine::GetPhysicsTickCount() const
{
    return m_PhysicsTickCounter.load(std::memory_order_relaxed);
}

void GameEngine::StartRuntimeThreads()
{
    if (m_ThreadedRuntimeActive.load()) {
        return;
    }

    m_ThreadManager = std::make_unique<ThreadManager>();
    m_ThreadManager->Start(
        // No separate game std::thread yet: the Win32/main loop calls Update().
        nullptr,
        // Physics worker: fixed-step simulation and snapshot publishing.
        [this]() { RunPhysicsThread(); },
        // Render worker: snapshot consumption and GPU frame submission.
        [this]() { RunRenderThread(); });
    m_ThreadedRuntimeActive.store(true);
}

void GameEngine::StopRuntimeThreads()
{
    // Flip both public runtime state and ThreadManager's loop flag, then wait
    // until every worker has returned before releasing system objects.
    m_ThreadedRuntimeActive.store(false);

    if (m_ThreadManager) {
        m_ThreadManager->RequestStop();
        m_ThreadManager->Join();
        m_ThreadManager.reset();
    }
}

void GameEngine::UpdateGameThread(float deltaTime)
{
    // Protect ECS-facing systems while physics/render workers may also read or
    // update shared components.
    std::lock_guard<std::mutex> lock(m_StateMutex);

    if (m_CameraSystem) m_CameraSystem->Update(deltaTime);
    // Store a value-type camera snapshot for the render thread.
    CaptureMainCameraLogicState();
    if (m_AnimationSystem) m_AnimationSystem->Update(deltaTime);
    m_GameFrameCounter.fetch_add(1, std::memory_order_relaxed);
}

void GameEngine::RunPhysicsThread()
{
    using clock = std::chrono::steady_clock;
    auto previousTime = clock::now();

    while (m_ThreadManager && m_ThreadManager->IsRunning()) {
        // Measure real elapsed time on the physics worker and feed it to the
        // scheduler, which converts it into zero or more fixed physics ticks.
        const auto currentTime = clock::now();
        const std::chrono::duration<float> delta = currentTime - previousTime;
        previousTime = currentTime;

        uint32_t fixedSteps = 0;
        {
            // Physics systems mutate transforms/rigidbodies, so keep them
            // serialized with the game and render phases for now.
            std::lock_guard<std::mutex> lock(m_StateMutex);
            if (m_PhysicsScheduler) {
                fixedSteps = m_PhysicsScheduler->Update(delta.count());
            }
        }
        m_PhysicsTickCounter.fetch_add(fixedSteps, std::memory_order_relaxed);

        // Avoid a hot spin when the scheduler has no fixed step to consume.
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void GameEngine::RunRenderThread()
{
    using clock = std::chrono::steady_clock;
    auto previousTime = clock::now();

    while (m_ThreadManager && m_ThreadManager->IsRunning()) {
        // Render has its own frame clock so it can run independently of the
        // main message loop and physics fixed-step cadence.
        const auto currentTime = clock::now();
        const std::chrono::duration<float> delta = currentTime - previousTime;
        previousTime = currentTime;

        {
            // Update render-side state while protected from concurrent ECS
            // changes and physics writes.
            std::lock_guard<std::mutex> lock(m_StateMutex);
            if (m_RenderSystem) {
                m_RenderSystem->Update(delta.count());
            }
        }

        if (m_RenderSystem) {
            // Render() drains command queues, reads latest snapshots, and
            // submits/presents the frame on this worker thread.
            m_RenderSystem->Render();
            m_RenderFrameCounter.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void GameEngine::CaptureMainCameraLogicState()
{
    // This function is called while m_StateMutex is already held.
    // It copies the live camera components into a thread-friendly value object.
    if (!m_MainCamera) {
        m_CameraLogicState.reset();
        return;
    }

    auto* transform = m_MainCamera->GetComponent<TransformComponent>();
    auto* camera = m_MainCamera->GetComponent<CameraComponent>();
    if (!transform || !camera) {
        m_CameraLogicState.reset();
        return;
    }

    CameraLogicState state;
    state.Owner = m_MainCamera->GetID();
    state.Position = transform->position;
    XMStoreFloat4(
        &state.Rotation,
        XMQuaternionRotationRollPitchYaw(transform->rotation.x, transform->rotation.y, transform->rotation.z));
    state.FovY = camera->Fov;
    state.AspectRatio = m_RenderSystem && m_RenderSystem->GetHeight() != 0
        ? static_cast<float>(m_RenderSystem->GetWidth()) / static_cast<float>(m_RenderSystem->GetHeight())
        : 16.0f / 9.0f;

    m_CameraLogicState = state;
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
    CaptureMainCameraLogicState();

    auto skyEntity = std::make_unique<Entity>(100);
    auto sky = skyEntity->AddComponent<SkyComponent>();
    sky->CubemapName = "snowcube1024";
    sky->Exposure = 1.0f;
    sky->RotationY = 0.0f;
    if (m_RenderSystem) {
        m_RenderSystem->SetActiveSky(skyEntity.get());
    }
    m_Entities.push_back(std::move(skyEntity));

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
        cube->GetComponent<MaterialComponent>()->albedoTextureName = "checkboard";

        m_PhysicsWorld->RegisterEntity(cube.get());
        m_RenderSystem->RegisterEntity(cube.get());
        m_Entities.push_back(std::move(cube));
    }

    // Ground
    auto ground = std::make_unique<Entity>(2);
    auto gt = ground->AddComponent<TransformComponent>();
    gt->SetPosition(0.0f, 0.0f, 0.0f);
    gt->SetScale(10.0f, 1.1f, 10.0f);
    gt->SetRotation(0.50f, 0.0f, 0.0f);
    auto rb = ground->AddComponent<RigidbodyComponent>();
    rb->isKinematic = true;
    ground->AddComponent<BoxCollider>()->size = { 1.0f, 1.0f, 1.0f };
    ground->AddComponent<MeshComponent>()->CreateCube();
    ground->AddComponent<MaterialComponent>()->SetAlbedo(0.3f, 0.8f, 0.3f);
    ground->GetComponent<MaterialComponent>()->albedoTextureName = "bricks3";

    m_PhysicsWorld->RegisterEntity(ground.get());
    m_RenderSystem->RegisterEntity(ground.get());
    m_Entities.push_back(std::move(ground));

    // FBX 모델 로드 예제
    // 주의: 실제 FBX 파일 경로로 변경해야 합니다.
    LoadFBXModel("Models/Nissan 180SX S13 (1992).fbx", 3, 90.0f, 45.0f, 0.0f, true);
    LoadFBXModel("Models/Dancing Twerk.fbx", 1, 0.0f, 0.0f, 0.0f, 0.05f, false);
}

void GameEngine::LoadFBXModel(const std::string& filePath, uint32_t entityId,
    float pitch, float yaw, float roll, float scale, bool attachMeshCollider)
{
    // 1. FBXLoader를 사용하여 FBX 파일 로드
    FBXLoader loader;
    Model* model = loader.Load(filePath);

    if (!model) {
        // 로드 실패 처리
        // Log::Error("Failed to load FBX file: %s", filePath.c_str());
        return;
    }

    // 2. Entity 생성 및 TransformComponent 추가
    auto fbxEntity = std::make_unique<Entity>(entityId);
    auto transform = fbxEntity->AddComponent<TransformComponent>();
    transform->SetPosition(0.0f, 2.0f, 0.0f);
    transform->SetRotationDegrees(pitch, yaw, roll);
    transform->SetScale(scale, scale, scale);

    // 3. Model을 Entity로 변환 (모든 메시를 하나로 병합)
    bool instantiated = ModelInstantiation::InstantiateToEntity(model, fbxEntity.get(), true);

    // 4. 메시 인스턴스화에 성공했을 때만 렌더/피직스 등록
    if (instantiated) {
        // RenderSystem에 등록 (GPU 버퍼 자동 생성)
        if (m_RenderSystem) {
            m_RenderSystem->RegisterEntity(fbxEntity.get());
        }

        // 5. MeshCollider 추가 (옵션)
        if (attachMeshCollider) {
            auto* meshComp = fbxEntity->GetComponent<MeshComponent>();
            if (meshComp) {
                auto* collider = fbxEntity->AddComponent<MeshCollider>();
                collider->meshComponent = meshComp;
                if (m_PhysicsWorld) {
                    m_PhysicsWorld->RegisterEntity(fbxEntity.get());
                }
            }
        }
    }

    // 6. Entity를 엔진에 추가
    m_Entities.push_back(std::move(fbxEntity));

    // 7. Model 메모리 해제
    delete model;
}

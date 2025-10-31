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

#include <Windows.h>
#include <memory>
#include <vector>

class GameEngine {
public:
    bool Initialize(HWND hwnd, uint32_t width, uint32_t height) {
        // Initialize Physics World
        m_PhysicsWorld = std::make_shared<PhysicsWorld>();
        m_PhysicsWorld->Initialize();

        // Add physics systems
        auto dynamicsSystem = std::make_unique<DynamicsSystem>(m_PhysicsWorld.get());
        dynamicsSystem->Initialize();
        m_PhysicsWorld->AddSystem(std::move(dynamicsSystem));

        auto collisionSystem = std::make_unique<CollisionSystem>(m_PhysicsWorld.get());
        collisionSystem->Initialize();
        m_PhysicsWorld->AddSystem(std::move(collisionSystem));

        // Initialize Physics Scheduler
        m_PhysicsScheduler = std::make_unique<PhysicsScheduler>(m_PhysicsWorld);

        // Initialize Render System
        m_RenderSystem = std::make_unique<RenderSystem>(hwnd, width, height);
        m_RenderSystem->Initialize();

        // Create game entities
        CreateEntities();

        return true;
    }

    void Update(float deltaTime) {
        // Physics update (fixed timestep)
        m_PhysicsScheduler->Update(deltaTime);

        // Rendering update (variable timestep)
        m_RenderSystem->Update(deltaTime);
    }

    void Render() {
        m_RenderSystem->Render();
    }

    void Shutdown() {
        m_RenderSystem->Shutdown();
        m_PhysicsWorld->Shutdown();

        m_Entities.clear();
    }

private:
    void CreateEntities() {
        
        // Create a cube entity with physics and rendering
        auto cubeEntity = std::make_unique<Entity>(1);

        // Add transform (shared between physics and rendering)
        auto transform = cubeEntity->AddComponent<TransformComponent>();
        transform->SetPosition(0.0f, 5.0f, 0.0f);
        transform->SetScale(1.0f, 1.0f, 1.0f);

        // Add physics components
        auto rigidbody = cubeEntity->AddComponent<RigidbodyComponent>();
        rigidbody->mass = 1.0f;
        rigidbody->useGravity = true;

        auto collider = cubeEntity->AddComponent<BoxCollider>();
        collider->size = { 1.0f, 1.0f, 1.0f };

        // Add rendering components
        auto mesh = cubeEntity->AddComponent<MeshComponent>();
        mesh->CreateCube();

        auto material = cubeEntity->AddComponent<MaterialComponent>();
        material->SetAlbedo(0.8f, 0.3f, 0.3f);

        // Register entity with systems
        m_PhysicsWorld->RegisterEntity(cubeEntity.get());
        m_RenderSystem->RegisterEntity(cubeEntity.get());

        m_Entities.push_back(std::move(cubeEntity));
        

        //auto skullEntity = std::make_unique<Entity>(3);

        //auto transform = skullEntity->AddComponent<TransformComponent>();
        //transform->SetPosition(0, 2, 0);
        //transform->SetScale(0.1f, 0.1f, 0.1f);
        //transform->SetRotation(0, 0.0f, 0.0f);

        //auto rigidbody = skullEntity->AddComponent<RigidbodyComponent>();
        //rigidbody->mass = 1.0f;
        //rigidbody->useGravity = true;
        //rigidbody->friction = 1.0f;

        //auto collider = skullEntity->AddComponent<BoxCollider>();
        //collider->size = { 3.0f,3.0f, 3.0f };

        //auto mesh = skullEntity->AddComponent<MeshComponent>();
        //mesh->LoadFromFile("Models/skull.txt");

        //auto material = skullEntity->AddComponent<MaterialComponent>();
        //material->SetAlbedo(1.0f, 1.0f, 1.0f);

        //m_PhysicsWorld->RegisterEntity(skullEntity.get());
        //m_RenderSystem->RegisterEntity(skullEntity.get());

        //m_Entities.push_back(std::move(skullEntity));

        // Create ground plane
        auto groundEntity = std::make_unique<Entity>(2);

        auto groundTransform = groundEntity->AddComponent<TransformComponent>();
        groundTransform->SetPosition(0.0f, 0.0f, 0.0f);
        groundTransform->SetScale(10.0f, 0.1f, 10.0f);
        groundTransform->SetRotation(0.0f, 0.0f, 10.0f);

        // Rigidbody 추가하되, Kinematic으로 설정하여 고정
        auto groundRb = groundEntity->AddComponent<RigidbodyComponent>();
        groundRb->isKinematic = true;  // 중요: 바닥은 움직이지 않음
        groundRb->useGravity = false;

        auto groundCollider = groundEntity->AddComponent<BoxCollider>();
        groundCollider->size = { 1.0f, 1.0f, 1.0f };

        auto groundMesh = groundEntity->AddComponent<MeshComponent>();
        groundMesh->CreateCube();

        auto groundMaterial = groundEntity->AddComponent<MaterialComponent>();
        groundMaterial->SetAlbedo(0.3f, 0.8f, 0.3f);

        m_PhysicsWorld->RegisterEntity(groundEntity.get());
        m_RenderSystem->RegisterEntity(groundEntity.get());

        m_Entities.push_back(std::move(groundEntity));
    }

private:
    std::shared_ptr<PhysicsWorld> m_PhysicsWorld;
    std::unique_ptr<PhysicsScheduler> m_PhysicsScheduler;
    std::unique_ptr<RenderSystem> m_RenderSystem;
    std::vector<std::unique_ptr<Entity>> m_Entities;
};

// Global variables
GameEngine g_Engine;
HWND g_Hwnd = nullptr;
bool g_Running = true;

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DESTROY:
        g_Running = false;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// WinMain entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"GameEngineWindowClass";
    const uint32_t WINDOW_WIDTH = 1280;
    const uint32_t WINDOW_HEIGHT = 720;

    // Register window class
    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;

    RegisterClassEx(&wc);

    // Create window
    RECT windowRect = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    g_Hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        L"Game Engine - Physics + Renderer",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!g_Hwnd) {
        return -1;
    }

    ShowWindow(g_Hwnd, nCmdShow);

    // Initialize engine
    if (!g_Engine.Initialize(g_Hwnd, WINDOW_WIDTH, WINDOW_HEIGHT)) {
        return -1;
    }

    // Main game loop
    MSG msg = {};
    LARGE_INTEGER frequency, lastTime, currentTime;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&lastTime);

    while (g_Running) {
        // Process messages
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Calculate delta time
        QueryPerformanceCounter(&currentTime);
        float deltaTime = static_cast<float>(currentTime.QuadPart - lastTime.QuadPart) / frequency.QuadPart;
        lastTime = currentTime;

        // Update and render
        g_Engine.Update(deltaTime);
        g_Engine.Render();
    }

    // Cleanup
    g_Engine.Shutdown();

    return 0;
}

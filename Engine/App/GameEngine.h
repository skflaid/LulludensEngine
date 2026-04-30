#pragma once
#include "Renderer/CameraState.h"
#include <Windows.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>
#include <cstdint>
#include <string>

class PhysicsWorld;
class PhysicsScheduler;
class RenderSystem;
class Entity;
class CameraSystem;
class AnimationSystem;
class SnapshotBuffer;
class ThreadManager;

class GameEngine
{
public:
    GameEngine();                 
    ~GameEngine();                

    GameEngine(const GameEngine&) = delete;
    GameEngine& operator=(const GameEngine&) = delete;

    GameEngine(GameEngine&&) noexcept = delete;
    GameEngine& operator=(GameEngine&&) noexcept = delete;

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height);
    void Update(float deltaTime);
    void Render();
    void Shutdown();
    // Starts/stops the runtime worker threads owned by ThreadManager.
    // The main game tick still runs from the Win32 loop through Update().
    void StartRuntimeThreads();
    void StopRuntimeThreads();
    bool IsThreadedRuntimeActive() const { return m_ThreadedRuntimeActive.load(); }

    uint64_t GetEntityCount() const;
    Entity* GetEntityByIndex(uint64_t index);

    // FBX 모델 로드 함수
    void LoadFBXModel(const std::string& filePath, uint32_t entityId, float pitch, float yaw, float roll, float scale = 1.f, bool attachMeshCollider = false);

    // 렌더링 모드 토글
    void ToggleRenderMode();
    void ToggleStyleTransfer();

    //카메라 관련 함수
    void SetMainCamera(Entity* camera);
    Entity* GetMainCamera() const;
    RenderSystem* GetRenderSystem() const { return m_RenderSystem.get(); }
    SnapshotBuffer* GetPhysicsSnapshotBuffer() const { return m_PhysicsSnapshotBuffer.get(); }
    float GetPhysicsInterpolationAlpha() const;
    std::optional<CameraLogicState> GetCameraLogicState() const;
    std::mutex& GetStateMutex() const { return m_StateMutex; }
    uint64_t GetGameFrameCount() const;
    uint64_t GetRenderFrameCount() const;
    uint64_t GetPhysicsTickCount() const;

private:
    void CreateEntities();
    void CaptureMainCameraLogicState();
    // Main/game-thread phase: input-facing systems update shared state here.
    void UpdateGameThread(float deltaTime);
    // Worker-thread phase: advances fixed-step physics and publishes snapshots.
    void RunPhysicsThread();
    // Worker-thread phase: consumes snapshots/commands and submits GPU work.
    void RunRenderThread();

private:
    // Coarse state lock used while the threaded runtime is being stabilized.
    // It prevents game, physics, render, and inspector code from mutating ECS
    // state at the same time.
    mutable std::mutex m_StateMutex;
    // Owns the std::thread objects and exposes a shared running flag.
    std::unique_ptr<ThreadManager> m_ThreadManager;
    // Fast cross-thread state/counters for diagnostics and fallback routing.
    std::atomic_bool m_ThreadedRuntimeActive = false;
    std::atomic_uint64_t m_GameFrameCounter = 0;
    std::atomic_uint64_t m_RenderFrameCounter = 0;
    std::atomic_uint64_t m_PhysicsTickCounter = 0;

    std::shared_ptr<PhysicsWorld> m_PhysicsWorld;
    // Physics thread publishes body transforms here; render thread reads them.
    std::shared_ptr<SnapshotBuffer> m_PhysicsSnapshotBuffer;
    std::unique_ptr<PhysicsScheduler> m_PhysicsScheduler;
    std::unique_ptr<RenderSystem>    m_RenderSystem;
    std::unique_ptr<CameraSystem> m_CameraSystem;
    std::unique_ptr<AnimationSystem> m_AnimationSystem;

    std::vector<std::unique_ptr<Entity>> m_Entities;
    Entity* m_MainCamera = nullptr;
    std::optional<CameraLogicState> m_CameraLogicState;
};

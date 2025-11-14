#pragma once
#include <Windows.h>
#include <memory>
#include <vector>
#include <cstdint>
#include <string>

class PhysicsWorld;
class PhysicsScheduler;
class RenderSystem;
class Entity;
class CameraSystem;

class GameEngine
{
public:
    GameEngine();                 // ����
    ~GameEngine();                // ����

    GameEngine(const GameEngine&) = delete;
    GameEngine& operator=(const GameEngine&) = delete;

    // �̵��� ����� '����' �ϰ�, ���Ǵ� .cpp����
    GameEngine(GameEngine&&) noexcept;
    GameEngine& operator=(GameEngine&&) noexcept;

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height);
    void Update(float deltaTime);
    void Render();
    void Shutdown();

    // ��ƼƼ ���ٿ� �ּ� �������̽��� ����
    uint64_t GetEntityCount() const;
    Entity* GetEntityByIndex(uint64_t index);

    // FBX 모델 로드 함수
    void LoadFBXModel(const std::string& filePath, uint32_t entityId, float pitch, float yaw, float roll);

    // 렌더링 모드 토글
    void ToggleRenderMode();

    //카메라 관련 함수
    void SetMainCamera(Entity* camera);
    Entity* GetMainCamera() const;
    RenderSystem* GetRenderSystem() const { return m_RenderSystem.get(); }

private:
    void CreateEntities();

private:
    std::shared_ptr<PhysicsWorld> m_PhysicsWorld;
    std::unique_ptr<PhysicsScheduler> m_PhysicsScheduler;
    std::unique_ptr<RenderSystem>    m_RenderSystem;
    std::vector<std::unique_ptr<Entity>> m_Entities;
    std::unique_ptr<CameraSystem> m_CameraSystem;
    Entity* m_MainCamera = nullptr;
};

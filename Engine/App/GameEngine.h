#pragma once
#include <Windows.h>
#include <memory>
#include <vector>
#include <cstdint>

class PhysicsWorld;
class PhysicsScheduler;
class RenderSystem;
class Entity;

class GameEngine
{
public:
    GameEngine();                 // 선언만
    ~GameEngine();                // 선언만

    GameEngine(const GameEngine&) = delete;
    GameEngine& operator=(const GameEngine&) = delete;

    // 이동도 헤더에 '선언만' 하고, 정의는 .cpp에서
    GameEngine(GameEngine&&) noexcept;
    GameEngine& operator=(GameEngine&&) noexcept;

    bool Initialize(HWND hwnd, uint32_t width, uint32_t height);
    void Update(float deltaTime);
    void Render();
    void Shutdown();

    // 엔티티 접근용 최소 인터페이스만 유지
    uint64_t GetEntityCount() const;
    Entity* GetEntityByIndex(uint64_t index);

private:
    void CreateEntities();

private:
    std::shared_ptr<PhysicsWorld> m_PhysicsWorld;
    std::unique_ptr<PhysicsScheduler> m_PhysicsScheduler;
    std::unique_ptr<RenderSystem>    m_RenderSystem;
    std::vector<std::unique_ptr<Entity>> m_Entities;
};

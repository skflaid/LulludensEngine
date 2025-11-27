#pragma once

class GameEngine;
class Entity;

class AnimationSystem
{
public:
    explicit AnimationSystem(GameEngine* engine)
        : m_Engine(engine)
    {
    }

    void Initialize() {}
    void Update(float deltaTime);

private:
    GameEngine* m_Engine = nullptr;

    void UpdateSkeletalAnimation(Entity* entity, float deltaTime);
};
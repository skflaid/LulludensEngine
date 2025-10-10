#pragma once
#include <vector>
#include <memory>

class Entity;

class ISystem {
public:
    virtual ~ISystem() = default;

    virtual void Initialize() = 0;
    virtual void Update(float deltaTime) = 0;
    virtual void Shutdown() = 0;

    virtual const char* GetName() const = 0;
};
#pragma once

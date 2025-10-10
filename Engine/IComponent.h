#pragma once
#include <typeinfo>
#include <memory>

class Entity; // Forward declaration

class IComponent {
public:
    virtual ~IComponent() = default;
    virtual void Initialize() {}
    virtual void Update(float deltaTime) {}
    virtual void Shutdown() {}

    // Component identification
    virtual const std::type_info& GetType() const = 0;

    // Entity reference
    void SetOwner(Entity* owner) { m_Owner = owner; }
    Entity* GetOwner() const { return m_Owner; }

protected:
    Entity* m_Owner = nullptr;
};

// Helper macro for component type identification
#define COMPONENT_TYPE(ClassName) \
    const std::type_info& GetType() const override { return typeid(ClassName); }

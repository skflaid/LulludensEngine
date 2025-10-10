#pragma once
#include "IComponent.h"
#include <vector>
#include <unordered_map>
#include <memory>
#include <typeindex>

class Entity {
public:
    Entity(uint32_t id) : m_ID(id) {}
    ~Entity() = default;

    uint32_t GetID() const { return m_ID; }

    // Component management
    template<typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        static_assert(std::is_base_of_v<IComponent, T>, "T must inherit from IComponent");

        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* componentPtr = component.get();
        component->SetOwner(this);

        m_Components[std::type_index(typeid(T))] = std::move(component);
        return componentPtr;
    }

    template<typename T>
    T* GetComponent() {
        auto it = m_Components.find(std::type_index(typeid(T)));
        if (it != m_Components.end()) {
            return static_cast<T*>(it->second.get());
        }
        return nullptr;
    }

    template<typename T>
    bool HasComponent() const {
        return m_Components.find(std::type_index(typeid(T))) != m_Components.end();
    }

    template<typename T>
    void RemoveComponent() {
        m_Components.erase(std::type_index(typeid(T)));
    }

    void Update(float deltaTime) {
        for (auto& [type, component] : m_Components) {
            component->Update(deltaTime);
        }
    }

private:
    uint32_t m_ID;
    std::unordered_map<std::type_index, std::unique_ptr<IComponent>> m_Components;
};

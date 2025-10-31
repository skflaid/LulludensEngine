#pragma once
#include "IComponent.h"
#include "Physics/Components/ColliderComponent.h" // 중요: 이 헤더를 포함해야 합니다.
#include <vector>
#include <unordered_map>
#include <memory>
#include <typeindex>

class Entity {
public:
    Entity(uint32_t id) : m_ID(id), m_Active(true) {}
    ~Entity() = default;

    uint32_t GetID() const { return m_ID; }
    bool IsActive() const { return m_Active; }
    void SetActive(bool active) { m_Active = active; }

    template<typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        static_assert(std::is_base_of_v<IComponent, T>, "T must inherit from IComponent");
        auto component = std::make_unique<T>(std::forward<Args>(args)...);
        T* componentPtr = component.get();
        component->SetOwner(this);
        component->Initialize();
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

    // 이 메서드는 Entity가 가진 컴포넌트 중 ColliderComponent 파생 클래스를 찾아 반환합니다.
    ColliderComponent* GetCollider() {
        for (const auto& pair : m_Components) {
            // pair.first는 type_index, pair.second는 unique_ptr<IComponent>
            auto& component = pair.second;

            ColliderComponent* collider = dynamic_cast<ColliderComponent*>(component.get());
            if (collider) {
                return collider;
            }
        }
        return nullptr;
    }

private:
    uint32_t m_ID;
    bool m_Active;
    std::unordered_map<std::type_index, std::unique_ptr<IComponent>> m_Components;
};
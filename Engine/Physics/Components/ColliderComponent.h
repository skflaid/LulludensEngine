#pragma once
#include "Core/IComponent.h"
#include <DirectXMath.h>

using namespace DirectX;

enum class ColliderType {
    Box,
    Sphere,
    Capsule,
    Mesh
};

struct ColliderComponent : public IComponent {
    COMPONENT_TYPE(ColliderComponent)

    ColliderType type = ColliderType::Box;
    bool isTrigger = false;
    XMFLOAT3 center = { 0.0f, 0.0f, 0.0f };

    // === Collision Response Data ===
    XMFLOAT3 contactPoint = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 contactNormal = { 0.0f, 1.0f, 0.0f };
    bool hasContact = false;
    float penetrationDepth = 0.0f;

    virtual ~ColliderComponent() = default;
};

struct BoxCollider : public ColliderComponent {
    COMPONENT_TYPE(BoxCollider)
    XMFLOAT3 size = { 1.0f, 1.0f, 1.0f };
    BoxCollider() { type = ColliderType::Box; }
};

struct SphereCollider : public ColliderComponent {
    COMPONENT_TYPE(SphereCollider)
    float radius = 0.5f;
    SphereCollider() { type = ColliderType::Sphere; }
};

struct CapsuleCollider : public ColliderComponent {
    COMPONENT_TYPE(CapsuleCollider)
    float radius = 0.5f;
    float height = 2.0f;
    CapsuleCollider() { type = ColliderType::Capsule; }
};

// Forward declaration
struct MeshComponent;

struct MeshCollider : public ColliderComponent {
    COMPONENT_TYPE(MeshCollider)
    // MeshComponent pointer (holds vertex data)
    MeshComponent* meshComponent = nullptr; 
    
    MeshCollider() { type = ColliderType::Mesh; }
};
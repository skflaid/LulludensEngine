#pragma once
#include "Core/IComponent.h"
#include <DirectXMath.h>

using namespace DirectX;

enum class ColliderType {
    Box,
    Sphere,
    Capsule
};

struct ColliderComponent : public IComponent {
    COMPONENT_TYPE(ColliderComponent)

    ColliderType type = ColliderType::Box;
    bool isTrigger = false;
    XMFLOAT3 center = { 0.0f, 0.0f, 0.0f };

    // === 경사면 물리를 위한 충돌 결과 필드 추가 ===
    XMFLOAT3 contactPoint = { 0.0f, 0.0f, 0.0f };   // 충돌 지점
    XMFLOAT3 contactNormal = { 0.0f, 1.0f, 0.0f };  // 충돌 표면 법선 (기본값: 위쪽)
    bool hasContact = false;                         // 이번 프레임에 충돌했는지 여부
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
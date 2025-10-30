#pragma once
#include "IComponent.h"
#include <DirectXMath.h>
#include <vector>

using namespace DirectX;

struct SoftBodyComponent : public IComponent {
    COMPONENT_TYPE(SoftBodyComponent)

        std::vector<XMFLOAT3> vertices;
    std::vector<XMFLOAT3> velocities;
    std::vector<float> masses;

    float stiffness = 1000.0f;
    float damping = 10.0f;
};

struct ClothComponent : public SoftBodyComponent {
    COMPONENT_TYPE(ClothComponent)

        uint32_t width, height;
    float particleDistance = 0.1f;

    struct Constraint {
        uint32_t particle1, particle2;
        float restLength;
    };
    std::vector<Constraint> constraints;
};

struct HairComponent : public SoftBodyComponent {
    COMPONENT_TYPE(HairComponent)

        uint32_t strandCount = 100;
    uint32_t segmentsPerStrand = 10;
    float segmentLength = 0.05f;
    XMFLOAT3 rootPosition = { 0.0f, 0.0f, 0.0f };
};
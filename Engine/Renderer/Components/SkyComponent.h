#pragma once
#include "Core/IComponent.h"
#include <DirectXMath.h>
#include <string>

using namespace DirectX;

enum class SkyType
{
    None,
    Cubemap,
    Procedural
};

struct SkyComponent : public IComponent
{
    COMPONENT_TYPE(SkyComponent)

    SkyType Type = SkyType::Cubemap;
    std::string CubemapName = "snowcube1024";

    float Exposure = 1.0f;
    float RotationY = 0.0f;
    XMFLOAT3 Tint = { 1.0f, 1.0f, 1.0f };

    bool Visible = true;
    bool AffectAmbientLighting = true;
    bool AffectReflection = true;
};

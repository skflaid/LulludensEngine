#pragma once
#include "Core/IComponent.h"
#include <DirectXMath.h>
#include <string>

using namespace DirectX;

struct MaterialComponent : public IComponent {
    COMPONENT_TYPE(MaterialComponent)

        XMFLOAT4 albedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    float metallic = 0.0f;
    float roughness = 0.5f;

    std::string shaderPath;
    
    // 텍스처 이름 (기본값: white1x1)
    std::string albedoTextureName = "white1x1";
    std::string normalTextureName = "white1x1";

    void SetAlbedo(float r, float g, float b, float a = 1.0f) {
        albedo = { r, g, b, a };
    }

    void SetShader(const std::string& path) {
        shaderPath = path;
    }
    
    void SetAlbedoTexture(const std::string& name) {
        albedoTextureName = name;
    }
    
    void SetNormalTexture(const std::string& name) {
        normalTextureName = name;
    }
};

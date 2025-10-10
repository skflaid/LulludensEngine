#pragma once
#include "IComponent.h"
#include <DirectXMath.h>

using namespace DirectX;

struct TransformComponent : public IComponent {
    COMPONENT_TYPE(TransformComponent)

        XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 rotation = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 scale = { 1.0f, 1.0f, 1.0f };

    XMMATRIX GetWorldMatrix() const {
        return XMMatrixScalingFromVector(XMLoadFloat3(&scale)) *
            XMMatrixRotationRollPitchYawFromVector(XMLoadFloat3(&rotation)) *
            XMMatrixTranslationFromVector(XMLoadFloat3(&position));
    }
};

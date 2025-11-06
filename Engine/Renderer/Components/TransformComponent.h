#pragma once
#include "IComponent.h"
#include <DirectXMath.h>

using namespace DirectX;

struct TransformComponent : public IComponent {
    COMPONENT_TYPE(TransformComponent)

        XMFLOAT3 position = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3 rotation = { 0.0f, 0.0f, 0.0f }; // Euler angles in radians
    XMFLOAT3 scale = { 1.0f, 1.0f, 1.0f };

    XMMATRIX GetWorldMatrix() const {
        XMMATRIX scaleMatrix = XMMatrixScalingFromVector(XMLoadFloat3(&scale));
        XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYawFromVector(XMLoadFloat3(&rotation));
        XMMATRIX translationMatrix = XMMatrixTranslationFromVector(XMLoadFloat3(&position));

        return scaleMatrix * rotationMatrix * translationMatrix;
    }

    void SetPosition(float x, float y, float z) {
        position = { x, y, z };
    }

    void SetRotation(float pitch, float yaw, float roll) {
        rotation = { pitch, yaw, roll };
    }

    // 각도(degree) 단위로 회전 설정 (라디안으로 자동 변환)
    void SetRotationDegrees(float pitchDeg, float yawDeg, float rollDeg) {
        rotation = { 
            XMConvertToRadians(pitchDeg), 
            XMConvertToRadians(yawDeg), 
            XMConvertToRadians(rollDeg) 
        };
    }

    void SetScale(float x, float y, float z) {
        scale = { x, y, z };
    }

    XMFLOAT3 GetPosition() const { return position; }
    XMFLOAT3 GetRotation() const { return rotation; }
    XMFLOAT3 GetScale() const { return scale; }
};

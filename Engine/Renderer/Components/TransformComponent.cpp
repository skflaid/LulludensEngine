#include "TransformComponent.h"

void TransformComponent::SetPosition(float x, float y, float z) { m_Position = { x, y, z }; }
void TransformComponent::SetPosition(const XMVECTOR& pos) { XMStoreFloat3(&m_Position, pos); }
void TransformComponent::SetRotation(float pitch, float yaw, float roll) { m_Rotation = { pitch, yaw, roll }; }
void TransformComponent::SetRotationDegrees(float pitchDeg, float yawDeg, float rollDeg) {
    m_Rotation = { XMConvertToRadians(pitchDeg), XMConvertToRadians(yawDeg), XMConvertToRadians(rollDeg) };
}

XMVECTOR TransformComponent::GetPositionVector() const { return XMLoadFloat3(&m_Position); }

XMMATRIX TransformComponent::GetViewMatrix() const {
    return XMMatrixLookToLH(GetPositionVector(), GetForwardVector(), GetUpVector());
}

XMVECTOR TransformComponent::GetForwardVector() const {
    XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYawFromVector(XMLoadFloat3(&m_Rotation));
    return XMVector3Normalize(XMVector3TransformCoord(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotationMatrix));
}

XMVECTOR TransformComponent::GetRightVector() const {
    XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYawFromVector(XMLoadFloat3(&m_Rotation));
    return XMVector3Normalize(XMVector3TransformCoord(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), rotationMatrix));
}

XMVECTOR TransformComponent::GetUpVector() const {
    return XMVector3Normalize(XMVector3Cross(GetForwardVector(), GetRightVector()));
}

#define NOMINMAX
#include "Systems/CameraSystem.h"
#include "App/GameEngine.h"
#include "Core/InputManager.h"
#include "Core/Entity.h"
#include "Renderer/Components/CameraComponent.h"
#include "Renderer/Components/TransformComponent.h"
#include <DirectXMath.h>
using namespace DirectX;

// ISystem 인터페이스 구현
CameraSystem::CameraSystem() = default;
CameraSystem::~CameraSystem() = default;
void CameraSystem::Initialize() {}
void CameraSystem::Shutdown() {}
const char* CameraSystem::GetName() const { return "CameraSystem"; }

// 메인 업데이트 로직
void CameraSystem::Update(float deltaTime) {
    if (!m_Engine) return;

    auto input = InputManager::Get();
    auto cameraEntity = m_Engine->GetMainCamera();
    if (!cameraEntity) return;

    auto cameraComp = cameraEntity->GetComponent<CameraComponent>();
    auto transformComp = cameraEntity->GetComponent<TransformComponent>();
    if (!cameraComp || !transformComp) return;

    // --- 1. 마우스 입력으로 회전 처리 ---
    POINT mouseDelta = input->GetMouseDelta();

    // 'public' 멤버인 rotation에 직접 접근
    transformComp->rotation.y += mouseDelta.x * cameraComp->LookSpeed * deltaTime; // Yaw
    transformComp->rotation.x += mouseDelta.y * cameraComp->LookSpeed * deltaTime; // Pitch

    // Pitch 각도 제한
    transformComp->rotation.x = std::max(-XM_PIDIV2 + 0.1f, std::min(XM_PIDIV2 - 0.1f, transformComp->rotation.x));

    // --- 2. 키보드 입력으로 이동 처리 ---
    // 'public' 멤버인 rotation과 position을 사용해 계산
    XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYawFromVector(XMLoadFloat3(&transformComp->rotation));
    XMVECTOR forward = XMVector3Normalize(XMVector3TransformCoord(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotationMatrix));
    XMVECTOR right = XMVector3Normalize(XMVector3TransformCoord(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), rotationMatrix));

    XMVECTOR position = XMLoadFloat3(&transformComp->position);
    if (input->IsKeyPressed('W')) { position += forward * cameraComp->MoveSpeed * deltaTime; }
    if (input->IsKeyPressed('S')) { position -= forward * cameraComp->MoveSpeed * deltaTime; }
    if (input->IsKeyPressed('A')) { position -= right * cameraComp->MoveSpeed * deltaTime; }
    if (input->IsKeyPressed('D')) { position += right * cameraComp->MoveSpeed * deltaTime; }
    XMStoreFloat3(&transformComp->position, position);

    // --- 3. View Matrix 갱신 ---
    XMVECTOR up = XMVector3Normalize(XMVector3Cross(forward, right));
    XMMATRIX viewMatrix = XMMatrixLookToLH(position, forward, up);
    XMStoreFloat4x4(&cameraComp->ViewMatrix, viewMatrix);
}
#define NOMINMAX
#include "CameraSystem.h"
#include "App/GameEngine.h"
#include "Core/InputManager.h"
#include "Core/Entity.h"
#include "Renderer/Components/CameraComponent.h"
#include "Renderer/Components/TransformComponent.h"
#include <DirectXMath.h>

using namespace DirectX;

// CameraSystem 생성자, 소멸자 및 ISystem 구현
CameraSystem::CameraSystem() = default;
CameraSystem::~CameraSystem() = default;
void CameraSystem::Initialize() {}
void CameraSystem::Shutdown() {}
const char* CameraSystem::GetName() const { return "CameraSystem"; }

void CameraSystem::SetGameEngine(GameEngine* engine)
{
    m_Engine = engine;
}

// 업데이트 함수
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

    // public 멤버인 rotation에 직접 접근
    // Mouse delta is already measured per frame, so applying deltaTime again
    // makes look sensitivity depend on FPS.
    transformComp->rotation.y += mouseDelta.x * cameraComp->LookSpeed; // Yaw
    transformComp->rotation.x += mouseDelta.y * cameraComp->LookSpeed; // Pitch

    // Pitch 각도 제한
    transformComp->rotation.x = std::max(-XM_PIDIV2 + 0.1f, std::min(XM_PIDIV2 - 0.1f, transformComp->rotation.x));

    // --- 2. 키보드 입력으로 이동 처리 ---
    XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYawFromVector(XMLoadFloat3(&transformComp->rotation));
    XMVECTOR forward = XMVector3Normalize(XMVector3TransformCoord(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotationMatrix));
    XMVECTOR right = XMVector3Normalize(XMVector3TransformCoord(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), rotationMatrix));
    XMVECTOR worldUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMVECTOR position = XMLoadFloat3(&transformComp->position);
    if (input->IsKeyPressed('W')) { position += forward * cameraComp->MoveSpeed * deltaTime; }
    if (input->IsKeyPressed('S')) { position -= forward * cameraComp->MoveSpeed * deltaTime; }
    if (input->IsKeyPressed('A')) { position -= right * cameraComp->MoveSpeed * deltaTime; }
    if (input->IsKeyPressed('D')) { position += right * cameraComp->MoveSpeed * deltaTime; }
    if (input->IsKeyPressed('E')) { position += worldUp * cameraComp->MoveSpeed * deltaTime; } // E 키: 위로 상승
    if (input->IsKeyPressed('Q')) { position -= worldUp * cameraComp->MoveSpeed * deltaTime; } // Q 키: 아래로 하강

    XMStoreFloat3(&transformComp->position, position);

    // --- 3. View Matrix 갱신 ---
    XMVECTOR up = XMVector3Normalize(XMVector3Cross(forward, right));
    XMMATRIX viewMatrix = XMMatrixLookToLH(position, forward, up);
    XMStoreFloat4x4(&cameraComp->ViewMatrix, viewMatrix);
}

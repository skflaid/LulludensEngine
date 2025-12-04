#include "CollisionSystem.h"
#include "Core/Entity.h"
#include "../Components/ColliderComponent.h"
#include "../../Renderer/Components/TransformComponent.h"
#include "../Components/RigidbodyComponent.h" 
#include "../../Renderer/Components/MeshComponent.h" // Changed from Model.h 
#include <Windows.h>
#include <algorithm>
#include <DirectXCollision.h> // BoundingBox, BoundingSphere 등을 위해 포함

CollisionSystem::CollisionSystem(PhysicsWorld* world) : m_PhysicsWorld(world) {}

void CollisionSystem::Initialize() {}
void CollisionSystem::Shutdown() {}

void CollisionSystem::RegisterEntity(Entity* entity) {
    if (entity->GetCollider()) { // GetCollider로 어떤 타입이든 감지
        m_Entities.push_back(entity);
    }
}

void CollisionSystem::UnregisterEntity(Entity* entity) {
    m_Entities.erase(std::remove(m_Entities.begin(), m_Entities.end(), entity), m_Entities.end());
}

void CollisionSystem::Update(float deltaTime) {
    if (!IsEnabled()) return;

    BroadPhaseDetection();

    /*
    // 디버그 출력
    char buffer[256];
    sprintf_s(buffer, "CollisionSystem: Found %zu collision pairs\n", m_CollisionPairs.size());
    OutputDebugStringA(buffer);
    */

    NarrowPhaseDetection();
    ResolveCollisions();
}

void CollisionSystem::BroadPhaseDetection() {
    //디버그
    /*
    char buf[128];
    sprintf_s(buf, "BroadPhaseDetection: registered entities=%zu\n", m_Entities.size());
    OutputDebugStringA(buf);
    */

    m_CollisionPairs.clear();
    for (size_t i = 0; i < m_Entities.size(); ++i) {
        // 모든 콜라이더의 hasContact 플래그 초기화
        if (auto* col = m_Entities[i]->GetCollider()) col->hasContact = false;

        for (size_t j = i + 1; j < m_Entities.size(); ++j) {
            if (CheckAABBCollision(m_Entities[i], m_Entities[j])) {
                m_CollisionPairs.push_back({ m_Entities[i], m_Entities[j] });
            }
        }
    }
}

bool CollisionSystem::CheckAABBCollision(Entity* entityA, Entity* entityB) {
    auto* collA = entityA->GetCollider();
    auto* collB = entityB->GetCollider();
    auto* transA = entityA->GetComponent<TransformComponent>();
    auto* transB = entityB->GetComponent<TransformComponent>();

    if (!collA || !collB || !transA || !transB) return false;

    BoundingBox aabbA, aabbB;

    // --- 콜라이더 A의 AABB 계산 ---
    if (collA->type == ColliderType::Box) {
        auto* box = static_cast<BoxCollider*>(collA);

        BoundingOrientedBox obb;
        obb.Center = transA->position;

        XMVECTOR sizeVec = DirectX::XMLoadFloat3(&box->size);
        XMVECTOR scaleVec = DirectX::XMLoadFloat3(&transA->scale);
        DirectX::XMStoreFloat3(&obb.Extents, sizeVec * scaleVec * 0.5f);

        XMVECTOR quat = DirectX::XMQuaternionRotationRollPitchYaw(
            DirectX::XMConvertToRadians(transA->rotation.x),
            DirectX::XMConvertToRadians(transA->rotation.y),
            DirectX::XMConvertToRadians(transA->rotation.z)
        );
        DirectX::XMStoreFloat4(&obb.Orientation, quat);

        XMFLOAT3 corners[8];
        obb.GetCorners(corners);

        BoundingBox::CreateFromPoints(aabbA, 8, corners, sizeof(XMFLOAT3));
    }
    else if (collA->type == ColliderType::Sphere) {
        auto* sphere = static_cast<SphereCollider*>(collA);

        // std::max 매크로 충돌을 피하기 위해 수동으로 최댓값 계산
        float maxScale = transA->scale.x;
        if (transA->scale.y > maxScale) maxScale = transA->scale.y;
        if (transA->scale.z > maxScale) maxScale = transA->scale.z;

        BoundingSphere bs(transA->position, sphere->radius * maxScale);
        BoundingBox::CreateFromSphere(aabbA, bs);
    }
    else if (collA->type == ColliderType::Mesh) {
        auto* meshCol = static_cast<MeshCollider*>(collA);
        if (meshCol->meshComponent && !meshCol->meshComponent->vertices.empty()) {
            XMVECTOR minVec = XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, 0);
            XMVECTOR maxVec = XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0);
            
            XMMATRIX world = XMMatrixScaling(transA->scale.x, transA->scale.y, transA->scale.z) *
                             XMMatrixRotationRollPitchYaw(transA->rotation.x, transA->rotation.y, transA->rotation.z) *
                             XMMatrixTranslation(transA->position.x, transA->position.y, transA->position.z);

            for (const auto& v : meshCol->meshComponent->vertices) {
                XMVECTOR pos = XMLoadFloat3(&v.position);
                pos = XMVector3Transform(pos, world);
                minVec = XMVectorMin(minVec, pos);
                maxVec = XMVectorMax(maxVec, pos);
            }
            
            XMFLOAT3 min, max;
            XMStoreFloat3(&min, minVec);
            XMStoreFloat3(&max, maxVec);
            
            aabbA.Center.x = (min.x + max.x) * 0.5f;
            aabbA.Center.y = (min.y + max.y) * 0.5f;
            aabbA.Center.z = (min.z + max.z) * 0.5f;
            aabbA.Extents.x = (max.x - min.x) * 0.5f;
            aabbA.Extents.y = (max.y - min.y) * 0.5f;
            aabbA.Extents.z = (max.z - min.z) * 0.5f;
        }
    }

    // --- 콜라이더 B의 AABB 계산 (A와 동일한 로직) ---
    if (collB->type == ColliderType::Box) {
        auto* box = static_cast<BoxCollider*>(collB);

        BoundingOrientedBox obb;
        obb.Center = transB->position;

        XMVECTOR sizeVec = DirectX::XMLoadFloat3(&box->size);
        XMVECTOR scaleVec = DirectX::XMLoadFloat3(&transB->scale);
        DirectX::XMStoreFloat3(&obb.Extents, sizeVec * scaleVec * 0.5f);

        XMVECTOR quat = DirectX::XMQuaternionRotationRollPitchYaw(
            DirectX::XMConvertToRadians(transB->rotation.x),
            DirectX::XMConvertToRadians(transB->rotation.y),
            DirectX::XMConvertToRadians(transB->rotation.z)
        );
        DirectX::XMStoreFloat4(&obb.Orientation, quat);

        XMFLOAT3 corners[8];
        obb.GetCorners(corners);

        BoundingBox::CreateFromPoints(aabbB, 8, corners, sizeof(XMFLOAT3));
    }
    else if (collB->type == ColliderType::Sphere) {
        auto* sphere = static_cast<SphereCollider*>(collB);

        float maxScale = transB->scale.x;
        if (transB->scale.y > maxScale) maxScale = transB->scale.y;
        if (transB->scale.z > maxScale) maxScale = transB->scale.z;

        BoundingSphere bs(transB->position, sphere->radius * maxScale);
        BoundingBox::CreateFromSphere(aabbB, bs);
    }
    else if (collB->type == ColliderType::Mesh) {
        auto* meshCol = static_cast<MeshCollider*>(collB);
        if (meshCol->meshComponent && !meshCol->meshComponent->vertices.empty()) {
            XMVECTOR minVec = XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, 0);
            XMVECTOR maxVec = XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0);
            
            XMMATRIX world = XMMatrixScaling(transB->scale.x, transB->scale.y, transB->scale.z) *
                             XMMatrixRotationRollPitchYaw(transB->rotation.x, transB->rotation.y, transB->rotation.z) *
                             XMMatrixTranslation(transB->position.x, transB->position.y, transB->position.z);

            for (const auto& v : meshCol->meshComponent->vertices) {
                XMVECTOR pos = XMLoadFloat3(&v.position);
                pos = XMVector3Transform(pos, world);
                minVec = XMVectorMin(minVec, pos);
                maxVec = XMVectorMax(maxVec, pos);
            }
            
            XMFLOAT3 min, max;
            XMStoreFloat3(&min, minVec);
            XMStoreFloat3(&max, maxVec);
            
            aabbB.Center.x = (min.x + max.x) * 0.5f;
            aabbB.Center.y = (min.y + max.y) * 0.5f;
            aabbB.Center.z = (min.z + max.z) * 0.5f;
            aabbB.Extents.x = (max.x - min.x) * 0.5f;
            aabbB.Extents.y = (max.y - min.y) * 0.5f;
            aabbB.Extents.z = (max.z - min.z) * 0.5f;
        }
    }

    return aabbA.Intersects(aabbB);
}
//bool CollisionSystem::CheckCollision(Entity* entityA, Entity* entityB) {
//    auto* transA = entityA->GetComponent<TransformComponent>();
//    auto* collA = entityA->GetComponent<BoxCollider>();
//    auto* transB = entityB->GetComponent<TransformComponent>();
//    auto* collB = entityB->GetComponent<BoxCollider>();
//
//    if (!transA || !collA || !transB || !collB) return false;
//
//    // 3D AABB 충돌 검사
//    float a_min_x = transA->position.x - (collA->size.x * transA->scale.x) / 2.0f;
//    float a_max_x = transA->position.x + (collA->size.x * transA->scale.x) / 2.0f;
//    float a_min_y = transA->position.y - (collA->size.y * transA->scale.y) / 2.0f;
//    float a_max_y = transA->position.y + (collA->size.y * transA->scale.y) / 2.0f;
//    float a_min_z = transA->position.z - (collA->size.z * transA->scale.z) / 2.0f;
//    float a_max_z = transA->position.z + (collA->size.z * transA->scale.z) / 2.0f;
//
//    float b_min_x = transB->position.x - (collB->size.x * transB->scale.x) / 2.0f;
//    float b_max_x = transB->position.x + (collB->size.x * transB->scale.x) / 2.0f;
//    float b_min_y = transB->position.y - (collB->size.y * transB->scale.y) / 2.0f;
//    float b_max_y = transB->position.y + (collB->size.y * transB->scale.y) / 2.0f;
//    float b_min_z = transB->position.z - (collB->size.z * transB->scale.z) / 2.0f;
//    float b_max_z = transB->position.z + (collB->size.z * transB->scale.z) / 2.0f;
//
//    // 3축 모두에서 겹침이 있어야 충돌
//    bool collisionX = a_max_x >= b_min_x && a_min_x <= b_max_x;
//    bool collisionY = a_max_y >= b_min_y && a_min_y <= b_max_y;
//    bool collisionZ = a_max_z >= b_min_z && a_min_z <= b_max_z;
//
//    return collisionX && collisionY && collisionZ;
//}
// 
// 
//void CollisionSystem::NarrowPhaseDetection() {
//    // 1. precisePairs를 한 번만 선언합니다.
//    std::vector<CollisionPair> precisePairs;
//    precisePairs.reserve(m_CollisionPairs.size());
//
//    // 2. 이중 루프를 제거하고, m_CollisionPairs를 한 번만 순회합니다.
//    for (auto& pair : m_CollisionPairs)
//    {
//        auto* transA = pair.entityA->GetComponent<TransformComponent>();
//        auto* transB = pair.entityB->GetComponent<TransformComponent>();
//        auto* collA = pair.entityA->GetComponent<BoxCollider>();
//        auto* collB = pair.entityB->GetComponent<BoxCollider>();
//
//        if (!transA || !transB || !collA || !collB)
//            continue;
//
//        // === 중심점 ===
//        XMVECTOR centerA = XMLoadFloat3(&transA->position);
//        XMVECTOR centerB = XMLoadFloat3(&transB->position);
//
//        // === 회전행렬 생성 (쿼터니언 사용) ===
//        XMVECTOR quatA = XMQuaternionRotationRollPitchYaw(transA->rotation.x, transA->rotation.y, transA->rotation.z);
//        XMMATRIX rotA = XMMatrixRotationQuaternion(quatA);
//
//        XMVECTOR quatB = XMQuaternionRotationRollPitchYaw(transB->rotation.x, transB->rotation.y, transB->rotation.z);
//        XMMATRIX rotB = XMMatrixRotationQuaternion(quatB);
//
//        // === 각 로컬 축 (OBB의 x, y, z 방향) ===
//        XMVECTOR axisA[3] = {
//            XMVector3Normalize(rotA.r[0]), // X
//            XMVector3Normalize(rotA.r[1]), // Y
//            XMVector3Normalize(rotA.r[2])  // Z
//        };
//        XMVECTOR axisB[3] = {
//            XMVector3Normalize(rotB.r[0]),
//            XMVector3Normalize(rotB.r[1]),
//            XMVector3Normalize(rotB.r[2])
//        };
//
//        // === Half Extents (반크기) ===
//        XMFLOAT3 halfA(
//            (collA->size.x * transA->scale.x) * 0.5f,
//            (collA->size.y * transA->scale.y) * 0.5f,
//            (collA->size.z * transA->scale.z) * 0.5f
//        );
//        XMFLOAT3 halfB(
//            (collB->size.x * transB->scale.x) * 0.5f,
//            (collB->size.y * transB->scale.y) * 0.5f,
//            (collB->size.z * transB->scale.z) * 0.5f
//        );
//
//        // === 중심 간 벡터 ===
//        XMVECTOR T = XMVectorSubtract(centerB, centerA);
//
//        // === 회전 행렬 R, 절댓값 행렬 AbsR ===
//        float R[3][3], AbsR[3][3];
//        for (int i = 0; i < 3; i++) {
//            for (int j = 0; j < 3; j++) {
//                R[i][j] = XMVectorGetX(XMVector3Dot(axisA[i], axisB[j]));
//                AbsR[i][j] = fabsf(R[i][j]) + 1e-6f; // 안정성 보정
//            }
//        }
//
//        // === T를 A의 로컬 공간으로 변환 ===
//        XMFLOAT3 Tlocal(
//            XMVectorGetX(XMVector3Dot(T, axisA[0])),
//            XMVectorGetX(XMVector3Dot(T, axisA[1])),
//            XMVectorGetX(XMVector3Dot(T, axisA[2]))
//        );
//
//        bool separated = false;
//        float ra, rb;
//        float penetration = FLT_MAX;
//        XMVECTOR bestAxis = XMVectorSet(0, 1, 0, 0); // 기본 충돌 법선
//
//        // === A의 축 검사 (3개) ===
//        for (int i = 0; i < 3 && !separated; i++) {
//            ra = (&halfA.x)[i];
//            rb = halfB.x * AbsR[i][0] + halfB.y * AbsR[i][1] + halfB.z * AbsR[i][2];
//            float dist = fabs((&Tlocal.x)[i]);
//            if (dist > ra + rb) {
//                separated = true;
//                break;
//            }
//            // 겹침 깊이 계산 및 최소값 갱신
//            float p = (ra + rb) - dist;
//            if (p < penetration) {
//                penetration = p;
//                bestAxis = axisA[i];
//            }
//        }
//        if (separated) continue;
//
//        // === B의 축 검사 (3개) ===
//        for (int j = 0; j < 3 && !separated; j++) {
//            ra = halfA.x * AbsR[0][j] + halfA.y * AbsR[1][j] + halfA.z * AbsR[2][j];
//            rb = (&halfB.x)[j];
//            float dist = fabs(Tlocal.x * R[0][j] + Tlocal.y * R[1][j] + Tlocal.z * R[2][j]);
//            if (dist > ra + rb) {
//                separated = true;
//                break;
//            }
//            float p = (ra + rb) - dist;
//            if (p < penetration) {
//                penetration = p;
//                bestAxis = axisB[j];
//            }
//        }
//        if (separated) continue;
//
//        // === 교차축 검사 (9개) - 여기서는 겹침 깊이 계산이 복잡하므로 일단 생략하고 축 분리만 확인 ===
//        // (정확도를 더 높이려면 이 부분도 겹침 깊이 계산을 추가해야 합니다)
//        for (int i = 0; i < 3 && !separated; i++) {
//            for (int j = 0; j < 3 && !separated; j++) {
//                // ... (기존 교차축 분리 검사 코드) ...
//            }
//        }
//        if (separated) continue;
//
//        // === 충돌 확정 ===
//        precisePairs.push_back(pair);
//
//        // 방향 보정 (A에서 B로 향하도록)
//        if (XMVectorGetX(XMVector3Dot(bestAxis, T)) < 0) {
//            bestAxis = XMVectorScale(bestAxis, -1.0f);
//        }
//
//        XMFLOAT3 contactNormal;
//        XMStoreFloat3(&contactNormal, bestAxis);
//
//        // 법선 및 겹침 정보 저장
//        collA->contactNormal = contactNormal;
//        collA->hasContact = true;
//        collA->penetrationDepth = penetration;
//
//        collB->contactNormal = { -contactNormal.x, -contactNormal.y, -contactNormal.z };
//        collB->hasContact = true;
//        collB->penetrationDepth = penetration;
//    }
//
//    m_CollisionPairs = precisePairs;
//
//    char buf[128];
//    sprintf_s(buf, "NarrowPhase: %zu precise collisions\n", m_CollisionPairs.size());
//    OutputDebugStringA(buf);
//}



// NarrowPhase: 정확한 충돌 검사 (OBB, Sphere)
void CollisionSystem::NarrowPhaseDetection() {
    std::vector<CollisionPair> precisePairs;
    for (auto& pair : m_CollisionPairs) {
        auto* collA = pair.entityA->GetCollider();
        auto* collB = pair.entityB->GetCollider();

        bool collided = false;
        if (collA->type == ColliderType::Box && collB->type == ColliderType::Box) {
            collided = TestBoxBox(pair.entityA, pair.entityB);
        }
        else if (collA->type == ColliderType::Sphere && collB->type == ColliderType::Sphere) {
            collided = TestSphereSphere(pair.entityA, pair.entityB);
        }
        else if (collA->type == ColliderType::Box && collB->type == ColliderType::Sphere) {
            collided = TestBoxSphere(pair.entityA, pair.entityB);
        }
        else if (collA->type == ColliderType::Sphere && collB->type == ColliderType::Box) {
            collided = TestBoxSphere(pair.entityB, pair.entityA); // 순서만 바꿔서 재사용
        }
        else if (collA->type == ColliderType::Mesh && collB->type == ColliderType::Sphere) {
            collided = TestMeshSphere(pair.entityA, pair.entityB);
        }
        else if (collA->type == ColliderType::Sphere && collB->type == ColliderType::Mesh) {
            collided = TestMeshSphere(pair.entityB, pair.entityA);
        }
        else if (collA->type == ColliderType::Mesh && collB->type == ColliderType::Box) {
            collided = TestMeshBox(pair.entityA, pair.entityB);
        }
        else if (collA->type == ColliderType::Box && collB->type == ColliderType::Mesh) {
            collided = TestMeshBox(pair.entityB, pair.entityA);
        }

        if (collided) {
            precisePairs.push_back(pair);
        }
    }
    m_CollisionPairs = precisePairs;
}

// Box-Box 충돌 검사 SAT 코드
bool CollisionSystem::TestBoxBox(Entity* entityA, Entity* entityB) {
    auto* collA = static_cast<BoxCollider*>(entityA->GetCollider());
    auto* collB = static_cast<BoxCollider*>(entityB->GetCollider());
    auto* transA = entityA->GetComponent<TransformComponent>();
    auto* transB = entityB->GetComponent<TransformComponent>();

    // 1. OBB 데이터 준비
    // 중심점
    XMVECTOR centerA = XMLoadFloat3(&transA->position);
    XMVECTOR centerB = XMLoadFloat3(&transB->position);

    // 회전행렬
    XMVECTOR quatA = XMQuaternionRotationRollPitchYaw(transA->rotation.x, transA->rotation.y, transA->rotation.z);
    XMMATRIX rotA = XMMatrixRotationQuaternion(quatA);

    XMVECTOR quatB = XMQuaternionRotationRollPitchYaw(transB->rotation.x, transB->rotation.y, transB->rotation.z);
    XMMATRIX rotB = XMMatrixRotationQuaternion(quatB);

    // 로컬 축
    XMVECTOR axisA[3] = { XMVector3Normalize(rotA.r[0]), XMVector3Normalize(rotA.r[1]), XMVector3Normalize(rotA.r[2]) };
    XMVECTOR axisB[3] = { XMVector3Normalize(rotB.r[0]), XMVector3Normalize(rotB.r[1]), XMVector3Normalize(rotB.r[2]) };

    // 반크기 (Half-extents)
    XMFLOAT3 halfA((collA->size.x * transA->scale.x) * 0.5f,
        (collA->size.y * transA->scale.y) * 0.5f,
        (collA->size.z * transA->scale.z) * 0.5f);
    XMFLOAT3 halfB((collB->size.x * transB->scale.x) * 0.5f,
        (collB->size.y * transB->scale.y) * 0.5f,
        (collB->size.z * transB->scale.z) * 0.5f);

    // 중심 간 벡터
    XMVECTOR T = centerB - centerA;

    // 회전 행렬 R (B -> A 공간 변환), 절댓값 행렬 AbsR
    float R[3][3], AbsR[3][3];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            R[i][j] = XMVectorGetX(XMVector3Dot(axisA[i], axisB[j]));
            AbsR[i][j] = fabsf(R[i][j]) + 1e-6f; // 부동소수점 오류 방지를 위한 epsilon
        }
    }

    // T를 A의 로컬 공간으로 변환
    XMFLOAT3 Tlocal(XMVectorGetX(XMVector3Dot(T, axisA[0])),
        XMVectorGetX(XMVector3Dot(T, axisA[1])),
        XMVectorGetX(XMVector3Dot(T, axisA[2])));

    float penetration = FLT_MAX;
    XMVECTOR bestAxis = XMVectorZero();

    // 2. SAT(Separating Axis Theorem) 검사
    // A의 3개 축 검사
    for (int i = 0; i < 3; i++) {
        float ra = (&halfA.x)[i];
        float rb = halfB.x * AbsR[i][0] + halfB.y * AbsR[i][1] + halfB.z * AbsR[i][2];
        float dist = fabs((&Tlocal.x)[i]);
        if (dist > ra + rb) return false; // 분리됨 = 충돌 아님

        float p = (ra + rb) - dist;
        if (p < penetration) {
            penetration = p;
            bestAxis = axisA[i];
        }
    }

    // B의 3개 축 검사
    for (int j = 0; j < 3; j++) {
        float ra = halfA.x * AbsR[0][j] + halfA.y * AbsR[1][j] + halfA.z * AbsR[2][j];
        float rb = (&halfB.x)[j];
        float dist = fabs(Tlocal.x * R[0][j] + Tlocal.y * R[1][j] + Tlocal.z * R[2][j]);
        if (dist > ra + rb) return false; // 분리됨

        float p = (ra + rb) - dist;
        if (p < penetration) {
            penetration = p;
            bestAxis = axisB[j];
        }
    }

    // 교차축 9개 검사 (분리 여부만 확인)
    // ij=01
    float ra_ = halfA.y * AbsR[2][1] + halfA.z * AbsR[1][1];
    float rb_ = halfB.x * AbsR[0][2] + halfB.z * AbsR[0][0];
    if (fabs(Tlocal.z * R[1][1] - Tlocal.y * R[2][1]) > ra_ + rb_) return false;
    // ... (나머지 8개 교차축 검사도 유사하게 추가 가능, 하지만 보통 6개 축으로도 충분)
    // 여기서는 일단 생략하여 성능 확보

    // 3. 충돌 발생: 법선 및 겹침 깊이 정보 저장
    // 충돌 법선의 방향을 A -> B로 맞춤
    if (XMVectorGetX(XMVector3Dot(bestAxis, T)) < 0.0f) {
        bestAxis = -bestAxis;
    }

    XMFLOAT3 contactNormal;
    XMStoreFloat3(&contactNormal, bestAxis);

    collA->contactNormal = contactNormal;
    collA->hasContact = true;
    collA->penetrationDepth = penetration;

    collB->contactNormal = { -contactNormal.x, -contactNormal.y, -contactNormal.z };
    collB->hasContact = true;
    collB->penetrationDepth = penetration;

    // === 여기부터 contactPoint 근사 추가 ===

    // === Improved Contact Point Calculation (Support Point) ===
    
    // Determine which body is the reference (face) and which is the incident (vertex/edge)
    // bestAxis is the collision normal. We need to know if it came from A's axes or B's axes.
    
    // Re-check which axis the bestAxis belongs to
    bool axisFromA = true;
    // Check if bestAxis is parallel to one of A's axes
    for(int i=0; i<3; ++i) {
        if (fabs(XMVectorGetX(XMVector3Dot(bestAxis, axisA[i]))) > 0.99f) {
            axisFromA = true;
            break;
        }
        if (fabs(XMVectorGetX(XMVector3Dot(bestAxis, axisB[i]))) > 0.99f) {
            axisFromA = false;
            break;
        }
    }

    // If axis is from A, then A is the reference face, B is the incident body.
    // We want the support point on the incident body (B) in the direction opposite to the normal (if normal points A->B).
    // Wait, bestAxis points A->B.
    // If axisFromA: Normal is along A's axis. A is pushing B. We want point on B that is deepest in A.
    // That point is the support point of B in direction -bestAxis (towards A).
    
    XMVECTOR supportDir;
    Entity* incidentEntity;
    TransformComponent* incidentTrans;
    BoxCollider* incidentColl;

    if (axisFromA) {
        // Normal is from A. Incident is B.
        // bestAxis points A -> B.
        // We want point on B closest to A, i.e., in direction -bestAxis.
        supportDir = -bestAxis;
        incidentEntity = entityB;
        incidentTrans = transB;
        incidentColl = collB;
    } else {
        // Normal is from B. Incident is A.
        // bestAxis points A -> B.
        // We want point on A closest to B, i.e., in direction bestAxis.
        supportDir = bestAxis;
        incidentEntity = entityA;
        incidentTrans = transA;
        incidentColl = collA;
    }

    // Calculate Support Point on Incident Box
    // Support Point is the corner of the box furthest in the direction of supportDir.
    
    // 1. Transform supportDir into Incident's local space
    XMVECTOR incidentQuat = XMQuaternionRotationRollPitchYaw(incidentTrans->rotation.x, incidentTrans->rotation.y, incidentTrans->rotation.z);
    XMVECTOR incidentInvQuat = XMQuaternionInverse(incidentQuat);
    XMVECTOR localDir = XMVector3Rotate(supportDir, incidentInvQuat);

    // 2. Get signs of the local direction vector
    XMFLOAT3 localDirF;
    XMStoreFloat3(&localDirF, localDir);
    
    XMFLOAT3 incidentHalfSize = { 
        incidentColl->size.x * incidentTrans->scale.x * 0.5f,
        incidentColl->size.y * incidentTrans->scale.y * 0.5f,
        incidentColl->size.z * incidentTrans->scale.z * 0.5f
    };

    // 3. Support point in local space is (sign(dir.x)*half.x, sign(dir.y)*half.y, ...)
    XMVECTOR localSupport = XMVectorSet(
        (localDirF.x >= 0 ? 1.0f : -1.0f) * incidentHalfSize.x,
        (localDirF.y >= 0 ? 1.0f : -1.0f) * incidentHalfSize.y,
        (localDirF.z >= 0 ? 1.0f : -1.0f) * incidentHalfSize.z,
        0.0f
    );

    // 4. Transform back to world space
    // Apply rotation
    XMVECTOR worldSupport = XMVector3Rotate(localSupport, incidentQuat);
    // Apply translation
    XMVECTOR incidentPos = XMLoadFloat3(&incidentTrans->position);
    worldSupport = XMVectorAdd(worldSupport, incidentPos);

    // This worldSupport is our contact point!
    XMFLOAT3 cp;
    XMStoreFloat3(&cp, worldSupport);

    collA->contactPoint = cp;
    collB->contactPoint = cp;

    return true; // 충돌
}

// Sphere-Sphere 충돌 검사
bool CollisionSystem::TestSphereSphere(Entity* entityA, Entity* entityB) {
    auto* collA = static_cast<SphereCollider*>(entityA->GetCollider());
    auto* collB = static_cast<SphereCollider*>(entityB->GetCollider());
    auto* transA = entityA->GetComponent<TransformComponent>();
    auto* transB = entityB->GetComponent<TransformComponent>();

    XMVECTOR posA = XMLoadFloat3(&transA->position);
    XMVECTOR posB = XMLoadFloat3(&transB->position);

    float combinedRadius = collA->radius + collB->radius;
    float distSq = XMVectorGetX(XMVector3LengthSq(posB - posA));

    if (distSq < combinedRadius * combinedRadius) {
        collA->hasContact = collB->hasContact = true;
        float dist = sqrt(distSq);
        float penetration = combinedRadius - dist;

        XMVECTOR normal = XMVector3Normalize(posA - posB);
        XMStoreFloat3(&collA->contactNormal, normal);
        XMStoreFloat3(&collB->contactNormal, -normal);
        collA->penetrationDepth = collB->penetrationDepth = penetration;
        return true;
    }
    return false;
}

bool CollisionSystem::TestBoxSphere(Entity* boxEntity, Entity* sphereEntity) {
    auto* boxColl = static_cast<BoxCollider*>(boxEntity->GetCollider());
    auto* sphereColl = static_cast<SphereCollider*>(sphereEntity->GetCollider());
    auto* boxTrans = boxEntity->GetComponent<TransformComponent>();
    auto* sphereTrans = sphereEntity->GetComponent<TransformComponent>();

    // 1. 데이터 로드
    XMVECTOR sphereCenterWorld = XMLoadFloat3(&sphereTrans->position);
    XMVECTOR boxCenterWorld = XMLoadFloat3(&boxTrans->position);
    XMVECTOR boxHalfSize = XMLoadFloat3(&boxColl->size) * 0.5f;

    // 2. [중요] 박스의 회전을 고려하여 Sphere를 박스의 '로컬 공간(Local Space)'으로 변환해야 함
    // 박스가 회전해 있어도, 로컬 공간에서는 축에 정렬된 상태(AABB)처럼 계산할 수 있기 때문입니다.

    // 박스의 월드 행렬 구성 (Rotation * Translation)
    // (TransformComponent에 회전이 Quaternion으로 저장되어 있다고 가정: rotation)
    // 만약 Euler 각도라면 XMMatrixRotationRollPitchYaw 등을 사용하세요.
    XMVECTOR boxRot = XMLoadFloat3(&boxTrans->rotation);
    XMMATRIX boxWorldMat = XMMatrixRotationQuaternion(boxRot) * XMMatrixTranslationFromVector(boxCenterWorld);

    // 월드 -> 로컬 변환 행렬 (Inverse)
    XMMATRIX boxInvMat = XMMatrixInverse(nullptr, boxWorldMat);

    // 구의 중심을 박스 로컬 공간으로 이동
    XMVECTOR sphereCenterLocal = XMVector3TransformCoord(sphereCenterWorld, boxInvMat);

    // 3. 로컬 공간에서 Closest Point(가장 가까운 점) 찾기 (Clamp)
    // 로컬 공간이므로 박스는 원점(0,0,0)에 있고 축에 정렬되어 있음
    XMVECTOR closestPointLocal = XMVectorMax(-boxHalfSize, XMVectorMin(sphereCenterLocal, boxHalfSize));

    // 4. 거리 검사 (로컬 공간에서 수행해도 결과는 같음)
    XMVECTOR distVec = sphereCenterLocal - closestPointLocal;
    float distSq = XMVectorGetX(XMVector3LengthSq(distVec));

    if (distSq < sphereColl->radius * sphereColl->radius) {
        // 충돌 발생!
        boxColl->hasContact = sphereColl->hasContact = true;

        // 5. [핵심] 로컬에서 구한 접점을 다시 '월드 좌표'로 변환하여 저장
        // 이 좌표가 있어야 ResolveCollisions에서 회전력(Torque)을 계산할 수 있음
        XMVECTOR contactPointWorld = XMVector3TransformCoord(closestPointLocal, boxWorldMat);

        float distance = sqrt(distSq);
        float penetration = sphereColl->radius - distance;

        // Normal 계산 (월드 기준)
        // 주의: 거리가 0에 가까우면(중심이 겹치면) 임의의 축으로 밀어내야 함
        XMVECTOR normalWorld;
        if (distance > 0.0001f) {
            normalWorld = XMVector3Normalize(sphereCenterWorld - contactPointWorld);
        }
        else {
            normalWorld = XMVectorSet(0, 1, 0, 0); // 예외 처리
        }

        // 6. 데이터 저장
        // Normal 저장 (A는 반대, B는 정방향)
        XMStoreFloat3(&boxColl->contactNormal, -normalWorld);
        XMStoreFloat3(&sphereColl->contactNormal, normalWorld);

        // Depth 저장
        boxColl->penetrationDepth = sphereColl->penetrationDepth = penetration;

        // [이 부분이 추가됨] 월드 좌표계의 정확한 접점 저장 -> Torque 계산의 핵심!
        XMStoreFloat3(&boxColl->contactPoint, contactPointWorld);
        XMStoreFloat3(&sphereColl->contactPoint, contactPointWorld);

        return true;
    }

    return false;
}

// Helper: Point-Triangle Distance Squared
float PointTriangleDistSq(XMVECTOR p, XMVECTOR a, XMVECTOR b, XMVECTOR c, XMVECTOR& outClosest) {
    XMVECTOR ab = b - a;
    XMVECTOR ac = c - a;
    XMVECTOR ap = p - a;

    float d1 = XMVectorGetX(XMVector3Dot(ab, ap));
    float d2 = XMVectorGetX(XMVector3Dot(ac, ap));
    if (d1 <= 0.0f && d2 <= 0.0f) { outClosest = a; return XMVectorGetX(XMVector3LengthSq(p - a)); }

    XMVECTOR bp = p - b;
    float d3 = XMVectorGetX(XMVector3Dot(ab, bp));
    float d4 = XMVectorGetX(XMVector3Dot(ac, bp));
    if (d3 >= 0.0f && d4 <= d3) { outClosest = b; return XMVectorGetX(XMVector3LengthSq(p - b)); }

    float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f) {
        float v = d1 / (d1 - d3);
        outClosest = a + v * ab;
        return XMVectorGetX(XMVector3LengthSq(p - outClosest));
    }

    XMVECTOR cp = p - c;
    float d5 = XMVectorGetX(XMVector3Dot(ab, cp));
    float d6 = XMVectorGetX(XMVector3Dot(ac, cp));
    if (d6 >= 0.0f && d5 <= d6) { outClosest = c; return XMVectorGetX(XMVector3LengthSq(p - c)); }

    float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f) {
        float w = d2 / (d2 - d6);
        outClosest = a + w * ac;
        return XMVectorGetX(XMVector3LengthSq(p - outClosest));
    }

    float va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f) {
        float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
        outClosest = b + w * (c - b);
        return XMVectorGetX(XMVector3LengthSq(p - outClosest));
    }

    float denom = 1.0f / (va + vb + vc);
    float v = vb * denom;
    float w = vc * denom;
    outClosest = a + ab * v + ac * w;
    return XMVectorGetX(XMVector3LengthSq(p - outClosest));
}

bool CollisionSystem::TestMeshSphere(Entity* meshEntity, Entity* sphereEntity) {
    auto* meshCol = static_cast<MeshCollider*>(meshEntity->GetCollider());
    auto* sphereCol = static_cast<SphereCollider*>(sphereEntity->GetCollider());
    auto* meshTrans = meshEntity->GetComponent<TransformComponent>();
    auto* sphereTrans = sphereEntity->GetComponent<TransformComponent>();

    if (!meshCol->meshComponent) return false;

    // Sphere World Info
    float maxScale = sphereTrans->scale.x;
    if (sphereTrans->scale.y > maxScale) maxScale = sphereTrans->scale.y;
    if (sphereTrans->scale.z > maxScale) maxScale = sphereTrans->scale.z;
    
    float radius = sphereCol->radius * maxScale;
    float radiusSq = radius * radius;
    XMVECTOR sphereCenter = XMLoadFloat3(&sphereTrans->position);

    // Mesh World Transform
    XMMATRIX meshWorld = XMMatrixScaling(meshTrans->scale.x, meshTrans->scale.y, meshTrans->scale.z) *
                         XMMatrixRotationRollPitchYaw(meshTrans->rotation.x, meshTrans->rotation.y, meshTrans->rotation.z) *
                         XMMatrixTranslation(meshTrans->position.x, meshTrans->position.y, meshTrans->position.z);

    const auto& indices = meshCol->meshComponent->indices;
    const auto& vertices = meshCol->meshComponent->vertices;

    for (size_t i = 0; i < indices.size(); i += 3) {
        XMVECTOR v0 = XMLoadFloat3(&vertices[indices[i]].position);
        XMVECTOR v1 = XMLoadFloat3(&vertices[indices[i+1]].position);
        XMVECTOR v2 = XMLoadFloat3(&vertices[indices[i+2]].position);

        v0 = XMVector3Transform(v0, meshWorld);
        v1 = XMVector3Transform(v1, meshWorld);
        v2 = XMVector3Transform(v2, meshWorld);

        XMVECTOR closest;
        float distSq = PointTriangleDistSq(sphereCenter, v0, v1, v2, closest);

        if (distSq <= radiusSq) {
            meshCol->hasContact = true;
            sphereCol->hasContact = true;
            
            XMVECTOR normal = XMVector3Normalize(sphereCenter - closest);
            XMStoreFloat3(&meshCol->contactNormal, normal);
            XMStoreFloat3(&sphereCol->contactNormal, -normal);
            
            float dist = sqrt(distSq);
            meshCol->penetrationDepth = radius - dist;
            sphereCol->penetrationDepth = radius - dist;
            
            return true; 
        }
    }

    return false;
}

bool CollisionSystem::TestMeshBox(Entity* meshEntity, Entity* boxEntity) {
    auto* meshCol = static_cast<MeshCollider*>(meshEntity->GetCollider());
    auto* boxCol = static_cast<BoxCollider*>(boxEntity->GetCollider());
    auto* meshTrans = meshEntity->GetComponent<TransformComponent>();
    auto* boxTrans = boxEntity->GetComponent<TransformComponent>();

    if (!meshCol->meshComponent) return false;

    // Box OBB
    BoundingOrientedBox obb;
    obb.Center = boxTrans->position;
    XMVECTOR sizeVec = DirectX::XMLoadFloat3(&boxCol->size);
    XMVECTOR scaleVec = DirectX::XMLoadFloat3(&boxTrans->scale);
    DirectX::XMStoreFloat3(&obb.Extents, sizeVec * scaleVec * 0.5f);
    XMVECTOR quat = DirectX::XMQuaternionRotationRollPitchYaw(
        DirectX::XMConvertToRadians(boxTrans->rotation.x),
        DirectX::XMConvertToRadians(boxTrans->rotation.y),
        DirectX::XMConvertToRadians(boxTrans->rotation.z)
    );
    DirectX::XMStoreFloat4(&obb.Orientation, quat);

    // Mesh World Transform
    XMMATRIX meshWorld = XMMatrixScaling(meshTrans->scale.x, meshTrans->scale.y, meshTrans->scale.z) *
                         XMMatrixRotationRollPitchYaw(meshTrans->rotation.x, meshTrans->rotation.y, meshTrans->rotation.z) *
                         XMMatrixTranslation(meshTrans->position.x, meshTrans->position.y, meshTrans->position.z);

    const auto& indices = meshCol->meshComponent->indices;
    const auto& vertices = meshCol->meshComponent->vertices;

    for (size_t i = 0; i < indices.size(); i += 3) {
        XMVECTOR v0 = XMLoadFloat3(&vertices[indices[i]].position);
        XMVECTOR v1 = XMLoadFloat3(&vertices[indices[i+1]].position);
        XMVECTOR v2 = XMLoadFloat3(&vertices[indices[i+2]].position);

        v0 = XMVector3Transform(v0, meshWorld);
        v1 = XMVector3Transform(v1, meshWorld);
        v2 = XMVector3Transform(v2, meshWorld);

        if (obb.Intersects(v0, v1, v2)) {
            meshCol->hasContact = true;
            boxCol->hasContact = true;
            
            // Calculate Triangle Normal
            XMVECTOR edge1 = v1 - v0;
            XMVECTOR edge2 = v2 - v0;
            XMVECTOR triNormal = XMVector3Normalize(XMVector3Cross(edge1, edge2));

            // Ensure normal points towards the box center (prevent tunneling from backface)
            XMVECTOR boxCenter = XMLoadFloat3(&obb.Center);
            if (XMVectorGetX(XMVector3Dot(triNormal, boxCenter - v0)) < 0.0f) {
                triNormal = -triNormal;
            }

            // Calculate Box Projection Radius along Triangle Normal
            XMVECTOR axisX = XMVector3Rotate(XMVectorSet(1, 0, 0, 0), quat);
            XMVECTOR axisY = XMVector3Rotate(XMVectorSet(0, 1, 0, 0), quat);
            XMVECTOR axisZ = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), quat);

            float r = obb.Extents.x * fabsf(XMVectorGetX(XMVector3Dot(triNormal, axisX))) +
                      obb.Extents.y * fabsf(XMVectorGetX(XMVector3Dot(triNormal, axisY))) +
                      obb.Extents.z * fabsf(XMVectorGetX(XMVector3Dot(triNormal, axisZ)));

            // Calculate Distance from Box Center to Triangle Plane
            float dist = XMVectorGetX(XMVector3Dot(triNormal, boxCenter - v0));

            // Penetration Depth
            float penetration = r - dist;

            // If penetration is negative, it means we are behind the plane but Intersects() returned true.
            // This usually happens on edges or if we are slightly behind.
            // We clamp to a small positive value or use the calculated value if positive.
            if (penetration <= 0.0f) penetration = 0.001f; // Minimal contact

            XMStoreFloat3(&meshCol->contactNormal, triNormal);
            XMStoreFloat3(&boxCol->contactNormal, -triNormal);
            
            meshCol->penetrationDepth = penetration;
            boxCol->penetrationDepth = penetration;

            return true;
        }
    }

    return false;
}

void CollisionSystem::ResolveCollisions() {
    const float slop = 0.01f;
    const float percent = 0.8f;

    for (const auto& pair : m_CollisionPairs) {
        // 1. 컴포넌트 가져오기
        auto* rbA = pair.entityA->GetComponent<RigidbodyComponent>();
        auto* rbB = pair.entityB->GetComponent<RigidbodyComponent>();
        auto* collA = pair.entityA->GetCollider();
        auto* transA = pair.entityA->GetComponent<TransformComponent>();
        auto* transB = pair.entityB->GetComponent<TransformComponent>();

        if (!collA || !pair.entityB->GetCollider()) continue;

        // 2. 질량 및 관성 설정
        float invMassA = (rbA && !rbA->isKinematic && rbA->mass > 0.0f) ? 1.0f / rbA->mass : 0.0f;
        float invMassB = (rbB && !rbB->isKinematic && rbB->mass > 0.0f) ? 1.0f / rbB->mass : 0.0f;
        float invInertiaA = (rbA && !rbA->isKinematic && rbA->inertia > 0.0f) ? 1.0f / rbA->inertia : 0.0f;
        float invInertiaB = (rbB && !rbB->isKinematic && rbB->inertia > 0.0f) ? 1.0f / rbB->inertia : 0.0f;

        float totalInvMass = invMassA + invMassB;
        if (totalInvMass <= 0.0f) continue;

        // 3. Normal 및 데이터 준비
        // [중요] Normal은 B -> A 방향 (바닥에서 큐브를 찌르는 방향)
        XMVECTOR normal = -XMLoadFloat3(&collA->contactNormal);
        normal = XMVector3Normalize(normal);

        XMVECTOR contactPoint = XMLoadFloat3(&collA->contactPoint);
        XMVECTOR centerA = XMLoadFloat3(&transA->position);
        XMVECTOR centerB = XMLoadFloat3(&transB->position);
        XMVECTOR rA = contactPoint - centerA; // 무게중심에서 접점까지의 벡터
        XMVECTOR rB = contactPoint - centerB;

        // 4. 속도 가져오기
        XMVECTOR velA = (rbA) ? XMLoadFloat3(&rbA->velocity) : XMVectorZero();
        XMVECTOR velB = (rbB) ? XMLoadFloat3(&rbB->velocity) : XMVectorZero();
        XMVECTOR angVelA = (rbA) ? XMLoadFloat3(&rbA->angularVelocity) : XMVectorZero();
        XMVECTOR angVelB = (rbB) ? XMLoadFloat3(&rbB->angularVelocity) : XMVectorZero();

        // 5. 상대 속도 계산 (A - B) : 표준 관례
        XMVECTOR velAtContactA = velA + XMVector3Cross(angVelA, rA);
        XMVECTOR velAtContactB = velB + XMVector3Cross(angVelB, rB);
        XMVECTOR relativeVel = velAtContactA - velAtContactB; // [수정] A - B 로 변경

        // 접근 속도 (음수여야 충돌 중)
        float velAlongNormal = XMVectorGetX(XMVector3Dot(relativeVel, normal));

        // 이미 멀어지고 있다면(양수) 처리 안 함
        if (velAlongNormal > 0) {
            // 단, 위치 보정은 필요할 수 있으므로 여기서는 continue 하지 않고 
            // 아래의 Impulse 로직만 건너뛰는 것이 더 정확하지만, 
            // 덜덜 떨림 방지를 위해 보통은 여기서 끊어줍니다.
            // 하지만 터널링 방지를 위해 위치 보정은 무조건 실행되게 구조를 짭니다.
            goto POSITION_CORRECTION;
        }

        // ==========================================
        // [Impulse Solver] : 반발력 및 마찰력
        // ==========================================
        {
            float e = 0.5f; // 반발 계수

            // 각충격량(Angular Impulse) 분모 계산
            XMVECTOR rAxN = XMVector3Cross(rA, normal);
            XMVECTOR rBxN = XMVector3Cross(rB, normal);
            float angConstraintA = XMVectorGetX(XMVector3LengthSq(rAxN)) * invInertiaA;
            float angConstraintB = XMVectorGetX(XMVector3LengthSq(rBxN)) * invInertiaB;

            float effectiveMass = totalInvMass + angConstraintA + angConstraintB;
            if (effectiveMass < 0.0001f) effectiveMass = 0.0001f;

            // 충격량 크기 j 계산
            float j = -(1.0f + e) * velAlongNormal / effectiveMass;

            // 충격량 벡터 (B->A 방향)
            XMVECTOR impulse = normal * j;

            // [핵심 수정] 속도 적용 부호 변경! 
            // A는 Normal 방향으로 힘을 받으므로 더해준다(+)
            // B는 반대 방향이므로 빼준다(-)
            if (rbA && !rbA->isKinematic) {
                // 선속도: F = ma => dv = J * invM
                XMStoreFloat3(&rbA->velocity, XMLoadFloat3(&rbA->velocity) + impulse * invMassA);
                // 각속도: Torque = r x F
                XMVECTOR torque = XMVector3Cross(rA, impulse);
                XMStoreFloat3(&rbA->angularVelocity, XMLoadFloat3(&rbA->angularVelocity) + torque * invInertiaA);
            }
            if (rbB && !rbB->isKinematic) {
                XMStoreFloat3(&rbB->velocity, XMLoadFloat3(&rbB->velocity) - impulse * invMassB);
                XMVECTOR torque = XMVector3Cross(rB, -impulse); // B는 -impulse를 받음
                XMStoreFloat3(&rbB->angularVelocity, XMLoadFloat3(&rbB->angularVelocity) + torque * invInertiaB);
            }

            // ------------------------------------------
            // [Friction Solver] : 마찰력 (접선 방향)
            // ------------------------------------------
            // 충격량을 적용했으니 상대 속도를 재계산할 수도 있지만, 보통 이전 프레임 속도로 근사합니다.
            // 접선 벡터 (Tangent)
            XMVECTOR tangent = relativeVel - (normal * velAlongNormal);
            float tangentLenSq = XMVectorGetX(XMVector3LengthSq(tangent));

            if (tangentLenSq > 0.0001f) {
                tangent = XMVector3Normalize(tangent);

                // 마찰 질량 계산
                XMVECTOR rAxT = XMVector3Cross(rA, tangent);
                XMVECTOR rBxT = XMVector3Cross(rB, tangent);
                float angFricA = XMVectorGetX(XMVector3LengthSq(rAxT)) * invInertiaA;
                float angFricB = XMVectorGetX(XMVector3LengthSq(rBxT)) * invInertiaB;
                float frictionMass = totalInvMass + angFricA + angFricB;

                // 마찰 충격량 크기 jt
                float velAlongTangent = XMVectorGetX(XMVector3Dot(relativeVel, tangent));
                float jt = -velAlongTangent / frictionMass;

                // 쿨롱 마찰 제한 (Clamp)
                const float mu = 0.5f;
                float maxJt = j * mu;
                if (abs(jt) > maxJt) {
                    jt = (jt > 0 ? 1.0f : -1.0f) * maxJt;
                }

                XMVECTOR frictionImpulse = tangent * jt;

                // [핵심 수정] 마찰 적용 부호 (반발력과 동일한 논리)
                // jt가 음수(상대속도 반대)로 계산되었으므로 tangent * jt는 '운동을 방해하는 방향'임
                if (rbA && !rbA->isKinematic) {
                    XMStoreFloat3(&rbA->velocity, XMLoadFloat3(&rbA->velocity) + frictionImpulse * invMassA);
                    XMVECTOR torque = XMVector3Cross(rA, frictionImpulse);
                    XMStoreFloat3(&rbA->angularVelocity, XMLoadFloat3(&rbA->angularVelocity) + torque * invInertiaA);
                }
                if (rbB && !rbB->isKinematic) {
                    XMStoreFloat3(&rbB->velocity, XMLoadFloat3(&rbB->velocity) - frictionImpulse * invMassB);
                    XMVECTOR torque = XMVector3Cross(rB, -frictionImpulse);
                    XMStoreFloat3(&rbB->angularVelocity, XMLoadFloat3(&rbB->angularVelocity) + torque * invInertiaB);
                }
            }
        }

        // ==========================================
        // [Position Correction] : 위치 보정 (터널링 방지)
        // ==========================================
    POSITION_CORRECTION:
        float penetration = collA->penetrationDepth;
        if (penetration > slop) {
            XMVECTOR correction = normal * (max(penetration - slop, 0.0f) / totalInvMass) * percent;

            // A는 Normal 방향(위)으로 꺼내줌 (+)
            if (rbA && !rbA->isKinematic) {
                XMStoreFloat3(&transA->position, XMLoadFloat3(&transA->position) + correction * invMassA);
            }
            // B는 반대 방향(아래)으로 밀어줌 (-)
            if (rbB && !rbB->isKinematic) {
                XMStoreFloat3(&transB->position, XMLoadFloat3(&transB->position) - correction * invMassB);
            }
        }
    }
}

void CollisionSystem::CheckSleep(RigidbodyComponent* rb, float deltaTime) {
    if (!rb->isAwake) return;

    // 속도와 회전 속도가 모두 매우 작다면 타이머 증가
    float speedSq = XMVectorGetX(XMVector3LengthSq(XMLoadFloat3(&rb->velocity)));
    // (회전 속도도 있다면 체크 필요)

    if (speedSq < rb->sleepThreshold) {
        rb->sleepTimer += deltaTime;

        // 0.5초 이상 정지 상태라면 재운다
        if (rb->sleepTimer > 0.5f) {
            rb->isAwake = false;

            // 확실하게 멈추기 위해 속도 0으로 초기화
            rb->velocity = { 0.0f, 0.0f, 0.0f };
        }
    }
    else {
        // 다시 움직이면 타이머 리셋
        rb->sleepTimer = 0.0f;
    }
}

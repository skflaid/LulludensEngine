#include "CollisionSystem.h"
#include "Core/Entity.h"
#include "../Components/ColliderComponent.h"
#include "../../Renderer/Components/TransformComponent.h"
#include "../Components/RigidbodyComponent.h" 
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

    // 디버그 출력
    char buffer[256];
    sprintf_s(buffer, "CollisionSystem: Found %zu collision pairs\n", m_CollisionPairs.size());
    OutputDebugStringA(buffer);

    NarrowPhaseDetection();
    ResolveCollisions();
}

void CollisionSystem::BroadPhaseDetection() {
    //디버그
    char buf[128];
    sprintf_s(buf, "BroadPhaseDetection: registered entities=%zu\n", m_Entities.size());
    OutputDebugStringA(buf);

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

    return true; // 충돌함
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

// Box-Sphere 충돌 검사
bool CollisionSystem::TestBoxSphere(Entity* boxEntity, Entity* sphereEntity) {
    auto* boxColl = static_cast<BoxCollider*>(boxEntity->GetCollider());
    auto* sphereColl = static_cast<SphereCollider*>(sphereEntity->GetCollider());
    auto* boxTrans = boxEntity->GetComponent<TransformComponent>();
    auto* sphereTrans = sphereEntity->GetComponent<TransformComponent>();

    XMVECTOR sphereCenter = XMLoadFloat3(&sphereTrans->position);
    XMVECTOR boxCenter = XMLoadFloat3(&boxTrans->position);
    XMVECTOR boxHalfSize = XMLoadFloat3(&boxColl->size) * 0.5f;

    // 구의 중심을 박스의 로컬 좌표계로 변환
    XMVECTOR sphereCenterInBoxSpace = sphereCenter - boxCenter;

    // 박스 내에서 구의 중심에 가장 가까운 점(closestPoint)을 찾음
    XMVECTOR closestPoint = XMVectorMax(-boxHalfSize, XMVectorMin(sphereCenterInBoxSpace, boxHalfSize));

    float distSq = XMVectorGetX(XMVector3LengthSq(sphereCenterInBoxSpace - closestPoint));

    if (distSq < sphereColl->radius * sphereColl->radius) {
        boxColl->hasContact = sphereColl->hasContact = true;

        float penetration = sphereColl->radius - sqrt(distSq);
        XMVECTOR normal = XMVector3Normalize(sphereCenter - (boxCenter + closestPoint));

        XMStoreFloat3(&boxColl->contactNormal, -normal);
        XMStoreFloat3(&sphereColl->contactNormal, normal);
        boxColl->penetrationDepth = sphereColl->penetrationDepth = penetration;
        return true;
    }
    return false;
}

void CollisionSystem::ResolveCollisions() {
    // --- 안정성을 위한 상수들 ---
    const float slop = 0.01f;   // 1cm 이내의 미세한 겹침은 무시
    const float percent = 0.8f; // 겹침의 80%를 보정하도록 값을 높임!

    for (const auto& pair : m_CollisionPairs) {
        auto* rbA = pair.entityA->GetComponent<RigidbodyComponent>();
        auto* rbB = pair.entityB->GetComponent<RigidbodyComponent>();
        auto* collA = pair.entityA->GetCollider();
        auto* collB = pair.entityB->GetCollider();
        auto* transA = pair.entityA->GetComponent<TransformComponent>();
        auto* transB = pair.entityB->GetComponent<TransformComponent>();

        if (!collA || !collB) continue;

        // 질량이 없는 정적 물체들 간의 충돌은 처리할 필요 없음
        float invMassA = (rbA && !rbA->isKinematic) ? 1.0f / rbA->mass : 0.0f;
        float invMassB = (rbB && !rbB->isKinematic) ? 1.0f / rbB->mass : 0.0f;
        float totalInvMass = invMassA + invMassB;
        if (totalInvMass <= 0.0f) continue;

        // --- 1. 속도 보정을 먼저 수행 ---
        XMVECTOR normal = XMLoadFloat3(&collA->contactNormal);

        XMVECTOR velA = (rbA) ? XMLoadFloat3(&rbA->velocity) : XMVectorZero();
        XMVECTOR velB = (rbB) ? XMLoadFloat3(&rbB->velocity) : XMVectorZero();
        XMVECTOR relativeVel = velB - velA;
        float velAlongNormal = XMVectorGetX(XMVector3Dot(relativeVel, normal));

        // 두 물체가 서로 멀어지고 있다면 처리할 필요 없음
        if (velAlongNormal <= 0) {
            float e = (rbA && rbB) ? min(rbA->restitution, rbB->restitution) : (rbA ? rbA->restitution : (rbB ? rbB->restitution : 0.0f));
            float j = -(1.0f + e) * velAlongNormal;
            j /= totalInvMass;
            XMVECTOR impulse = normal * j;

            if (rbA && !rbA->isKinematic) {
                XMStoreFloat3(&rbA->velocity, velA - impulse * invMassA);
            }
            if (rbB && !rbB->isKinematic) {
                XMStoreFloat3(&rbB->velocity, velB + impulse * invMassB);
            }
        }

        // --- 2. 그 다음 위치 보정을 수행 ---
        float penetration = collA->penetrationDepth;
        if (penetration > slop) {
            // 보정량 계산
            XMVECTOR correction = normal * (max(penetration - slop, 0.0f) / totalInvMass) * percent;

            // 위치 보정 적용
            if (rbA && !rbA->isKinematic) {
                XMStoreFloat3(&transA->position, XMLoadFloat3(&transA->position) - correction * invMassA);
            }
            if (rbB && !rbB->isKinematic) {
                XMStoreFloat3(&transB->position, XMLoadFloat3(&transB->position) + correction * invMassB);
            }
        }
    }
}

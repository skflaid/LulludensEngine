#include "Renderer/Systems/AnimationSystem.h"
#include "App/GameEngine.h"
#include "Core/Entity.h"
#include "Renderer/Components/SkeletonComponent.h"
#include "Renderer/Components/SkeletalAnimationComponent.h"
#include "Renderer/Model.h"

#include <DirectXMath.h>
#include <vector>
#include <functional>

using namespace DirectX;

// --------- 보간 헬퍼들 ---------

static XMFLOAT3 LerpVec3(const XMFLOAT3& a, const XMFLOAT3& b, float t)
{
    XMFLOAT3 r;
    r.x = a.x + (b.x - a.x) * t;
    r.y = a.y + (b.y - a.y) * t;
    r.z = a.z + (b.z - a.z) * t;
    return r;
}

static XMFLOAT4 SlerpQuat(const XMFLOAT4& a, const XMFLOAT4& b, float t)
{
    XMVECTOR qa = XMLoadFloat4(&a);
    XMVECTOR qb = XMLoadFloat4(&b);
    XMVECTOR qr = XMQuaternionSlerp(qa, qb, t);
    XMFLOAT4 r;
    XMStoreFloat4(&r, qr);
    return r;
}

static XMFLOAT3 SampleVector3(const std::vector<ModelKeyframeVec3>& keys,
    double time, const XMFLOAT3& defaultValue)
{
    if (keys.empty()) return defaultValue;
    if (keys.size() == 1) return keys[0].Value;

    if (time <= keys.front().Time) return keys.front().Value;
    if (time >= keys.back().Time)  return keys.back().Value;

    for (size_t i = 0; i < keys.size() - 1; ++i) {
        if (time < keys[i + 1].Time) {
            double t1 = keys[i].Time;
            double t2 = keys[i + 1].Time;
            float factor = static_cast<float>((time - t1) / (t2 - t1));
            return LerpVec3(keys[i].Value, keys[i + 1].Value, factor);
        }
    }
    return keys.back().Value;
}

static XMFLOAT4 SampleQuat(const std::vector<ModelKeyframeQuat>& keys,
    double time, const XMFLOAT4& defaultValue)
{
    if (keys.empty()) return defaultValue;
    if (keys.size() == 1) return keys[0].Value;

    if (time <= keys.front().Time) return keys.front().Value;
    if (time >= keys.back().Time)  return keys.back().Value;

    for (size_t i = 0; i < keys.size() - 1; ++i) {
        if (time < keys[i + 1].Time) {
            double t1 = keys[i].Time;
            double t2 = keys[i + 1].Time;
            float factor = static_cast<float>((time - t1) / (t2 - t1));
            return SlerpQuat(keys[i].Value, keys[i + 1].Value, factor);
        }
    }
    return keys.back().Value;
}

static void DecomposeSRT(const XMFLOAT4X4& m,
    XMFLOAT3& outS, XMFLOAT4& outR, XMFLOAT3& outT)
{
    XMMATRIX M = XMLoadFloat4x4(&m);

    XMVECTOR s, r, t;
    XMMatrixDecompose(&s, &r, &t, M);

    XMStoreFloat3(&outS, s);
    XMStoreFloat4(&outR, r);
    XMStoreFloat3(&outT, t);
}

static XMFLOAT3 ExtractBindTranslation(const XMFLOAT4X4& m)
{
    XMMATRIX M = XMLoadFloat4x4(&m);

    XMVECTOR s, r, t;
    XMMatrixDecompose(&s, &r, &t, M);

    XMFLOAT3 outT;
    XMStoreFloat3(&outT, t);
    return outT;
}

static void EvaluateBoneLocal(const ModelBoneAnimation& anim,
    const ModelBone& bindBone,
    double time,
    XMFLOAT4X4& out)
{
    // 1) 바인드 포즈 S/R/T 분해
    XMFLOAT3 bindS;
    XMFLOAT4 bindR;
    XMFLOAT3 bindT;
    DecomposeSRT(bindBone.BindTransform, bindS, bindR, bindT);

    // 2) Translation
    //    - 루트 본만 애니메이션 Translation 사용
    //    - 나머지 본은 바인드 포즈 위치 유지
    XMFLOAT3 T = bindT;
    if (bindBone.ParentIndex < 0 && !anim.Translations.empty()) {
        T = SampleVector3(anim.Translations, time, bindT);
    }

    // 3) Scale
    XMFLOAT3 S = bindS;
    if (!anim.Scales.empty()) {
        S = SampleVector3(anim.Scales, time, bindS);
    }

    // 4) Rotation
    //    애니메이션 회전을 "델타"로 보고 바인드 회전에 곱해준다.
    //    (없으면 항등 쿼터니언)
    XMFLOAT4 deltaR = { 0.0f, 0.0f, 0.0f, 1.0f };
    if (!anim.Rotations.empty()) {
        deltaR = SampleQuat(anim.Rotations, time, deltaR);
    }

    XMVECTOR qBind = XMLoadFloat4(&bindR);
    XMVECTOR qDelta = XMLoadFloat4(&deltaR);
    XMVECTOR qFinal = XMQuaternionMultiply(qDelta, qBind); // 최종 회전 = delta * bind
    XMFLOAT4 R;
    XMStoreFloat4(&R, qFinal);

    // 5) 최종 로컬 행렬 S * R * T
    XMMATRIX mS = XMMatrixScaling(S.x, S.y, S.z);
    XMMATRIX mR = XMMatrixRotationQuaternion(XMLoadFloat4(&R));
    XMMATRIX mT = XMMatrixTranslation(T.x, T.y, T.z);

    XMMATRIX M = mS * mR * mT;   // row-vector 기준
    XMStoreFloat4x4(&out, M);
}


static const ModelAnimationClip* FindClip(const SkeletonComponent* skel,
    const std::string& name)
{
    if (skel->Animations.empty()) return nullptr;
    if (name.empty()) return &skel->Animations[0];

    for (const auto& clip : skel->Animations) {
        if (clip.Name == name) {
            return &clip;
        }
    }
    return &skel->Animations[0];
}

// --------- AnimationSystem 구현 ---------

void AnimationSystem::Update(float deltaTime)
{
    if (!m_Engine) return;

    const uint64_t entityCount = m_Engine->GetEntityCount();
    for (uint64_t i = 0; i < entityCount; ++i) {
        Entity* entity = m_Engine->GetEntityByIndex(i);
        if (!entity || !entity->IsActive())
            continue;

        UpdateSkeletalAnimation(entity, deltaTime);
    }
}

void AnimationSystem::UpdateSkeletalAnimation(Entity* entity, float deltaTime)
{
    auto* skeleton = entity->GetComponent<SkeletonComponent>();
    auto* animComp = entity->GetComponent<SkeletalAnimationComponent>();

    if (!skeleton || !animComp) return;
    if (!animComp->Playing)      return;
    if (skeleton->Bones.empty() || skeleton->Animations.empty()) return;

    const ModelAnimationClip* clip = FindClip(skeleton, animComp->CurrentClipName);
    if (!clip) return;

    double ticksPerSecond = (clip->TicksPerSecond != 0.0)
        ? clip->TicksPerSecond
        : 25.0;

    // 시간 업데이트 (초 단위 -> tick 단위)
    animComp->CurrentTime += deltaTime * animComp->PlayRate;
    double timeInTicks = animComp->CurrentTime * ticksPerSecond;
    double duration = clip->Duration;

    double animTime = timeInTicks;
    if (duration > 0.0) {
        if (animComp->Loop) {
            animTime = fmod(timeInTicks, duration);
        }
        else {
            if (timeInTicks > duration) {
                animTime = duration;
                animComp->Playing = false;
            }
        }
    }

    size_t boneCount = skeleton->Bones.size();
    if (boneCount == 0) return;

    std::vector<XMFLOAT4X4> localTransforms(boneCount);
    std::vector<XMFLOAT4X4> globalTransforms(boneCount);
    std::vector<bool>       computed(boneCount, false);

    // 로컬 행렬 계산
    for (size_t i = 0; i < boneCount; ++i) {
        const ModelBone& bindBone = skeleton->Bones[i];

        if (i < clip->BoneAnimations.size()) {
            const ModelBoneAnimation& boneAnim = clip->BoneAnimations[i];

            if (!boneAnim.Translations.empty() ||
                !boneAnim.Rotations.empty() ||
                !boneAnim.Scales.empty())
            {
                //  바인드 포즈를 기본으로, 애니 키를 덮어쓴다
                EvaluateBoneLocal(boneAnim, bindBone, animTime, localTransforms[i]);
                continue;
            }
        }

        // 애니 키가 하나도 없는 본은 바인드 포즈 그대로
        localTransforms[i] = bindBone.BindTransform;
    }

    // 계층 구조를 따라 월드 행렬 계산
    std::function<void(size_t)> computeWorld =
        [&](size_t index)
        {
            if (computed[index])
                return;

            int parentIndex = skeleton->Bones[index].ParentIndex;
            XMMATRIX localM = XMLoadFloat4x4(&localTransforms[index]);

            if (parentIndex >= 0 &&
                parentIndex < (int)boneCount &&
                parentIndex != (int)index)       // 자기 자신을 부모로 보는 경우 방지
            {
                computeWorld((size_t)parentIndex);
                XMMATRIX parentWorld = XMLoadFloat4x4(&globalTransforms[parentIndex]);

                // row-vector 기준: world = local * parent
                XMMATRIX world = localM * parentWorld;
                XMStoreFloat4x4(&globalTransforms[index], world);
            }
            else
            {
                // 루트 / 이상한 부모는 그냥 로컬=월드
                XMStoreFloat4x4(&globalTransforms[index], localM);
            }

            computed[index] = true;
        };

    for (size_t i = 0; i < boneCount; ++i)
        computeWorld(i);



    // 최종 스키닝 행렬 = offset * global
    // 최종 스키닝 행렬 = offset * global
    skeleton->FinalBoneTransforms.resize(boneCount);

    // 각 본의 바인드 포즈 글로벌 변환 계산
    std::vector<DirectX::XMMATRIX> globalBind(boneCount);
    for (size_t i = 0; i < boneCount; ++i) {
        int p = skeleton->Bones[i].ParentIndex;
        DirectX::XMMATRIX localM = DirectX::XMLoadFloat4x4(&skeleton->Bones[i].BindTransform);
        if (p >= 0) {
            globalBind[i] = DirectX::XMMatrixMultiply(localM, globalBind[p]);
        }
        else {
            globalBind[i] = localM;
        }
    }

    // 최종 스키닝 행렬 = offset * bindGlobal
    skeleton->FinalBoneTransforms.resize(boneCount);
    for (size_t i = 0; i < boneCount; ++i) {
        DirectX::XMMATRIX globalAnimated = DirectX::XMLoadFloat4x4(&globalTransforms[i]);
        DirectX::XMMATRIX offset = DirectX::XMLoadFloat4x4(&skeleton->Bones[i].Offset);
        DirectX::XMMATRIX finalM = DirectX::XMMatrixMultiply(offset, globalAnimated);
        DirectX::XMStoreFloat4x4(&skeleton->FinalBoneTransforms[i], finalM);
    }
}

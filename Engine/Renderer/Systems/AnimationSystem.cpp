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

static void EvaluateBoneLocal(const ModelBoneAnimation& anim,
    double time, XMFLOAT4X4& out)
{
    XMFLOAT3 T = SampleVector3(anim.Translations, time, XMFLOAT3(0, 0, 0));
    XMFLOAT3 S = SampleVector3(anim.Scales, time, XMFLOAT3(1, 1, 1));
    XMFLOAT4 R = SampleQuat(anim.Rotations, time, XMFLOAT4(0, 0, 0, 1));

    XMMATRIX mS = XMMatrixScaling(S.x, S.y, S.z);
    XMMATRIX mR = XMMatrixRotationQuaternion(XMLoadFloat4(&R));
    XMMATRIX mT = XMMatrixTranslation(T.x, T.y, T.z);

    XMMATRIX M = mS * mR * mT;
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
        if (i < clip->BoneAnimations.size()) {
            const ModelBoneAnimation& boneAnim = clip->BoneAnimations[i];
            if (!boneAnim.Translations.empty() ||
                !boneAnim.Rotations.empty() ||
                !boneAnim.Scales.empty())
            {
                EvaluateBoneLocal(boneAnim, animTime, localTransforms[i]);
                continue;
            }
        }
        XMStoreFloat4x4(&localTransforms[i], XMMatrixIdentity());
    }

    // 계층 구조를 따라 월드 행렬 계산
    std::function<void(size_t)> computeWorld =
        [&](size_t index)
        {
            if (computed[index]) return;

            int parentIndex = skeleton->Bones[index].ParentIndex;
            XMMATRIX localM = XMLoadFloat4x4(&localTransforms[index]);

            if (parentIndex >= 0) {
                computeWorld(parentIndex);
                XMMATRIX parentWorld = XMLoadFloat4x4(&globalTransforms[parentIndex]);
                XMMATRIX world = localM * parentWorld;
                XMStoreFloat4x4(&globalTransforms[index], world);
            }
            else {
                XMStoreFloat4x4(&globalTransforms[index], localM);
            }

            computed[index] = true;
        };

    for (size_t i = 0; i < boneCount; ++i) {
        computeWorld(i);
    }

    // 최종 스키닝 행렬 = Global * Offset (또는 Offset * Global, 나중에 필요하면 바꿔봄)
    skeleton->FinalBoneTransforms.resize(boneCount);
    for (size_t i = 0; i < boneCount; ++i) {
        XMMATRIX global = XMLoadFloat4x4(&globalTransforms[i]);
        XMMATRIX offset = XMLoadFloat4x4(&skeleton->Bones[i].Offset);

        // 흔히 쓰는 형태는 global * offset (Assimp 기준)
        XMMATRIX finalM = global * offset;
        XMStoreFloat4x4(&skeleton->FinalBoneTransforms[i], finalM);
    }
}

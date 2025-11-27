#pragma once
#include "Core/IComponent.h"
#include <string>

struct SkeletalAnimationComponent : public IComponent {
    COMPONENT_TYPE(SkeletalAnimationComponent)

        std::string CurrentClipName;  // 재생 중인 클립 이름
    float CurrentTime = 0.0f;     // 초 단위
    float PlayRate = 1.0f;      // 재생 속도 배수
    bool  Loop = true;      // 루프 여부
    bool  Playing = true;      // 일시정지/재생

    void Initialize() override {}
    void Shutdown() override {}
};

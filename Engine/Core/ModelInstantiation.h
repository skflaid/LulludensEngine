#pragma once
#include "Renderer/Model.h"

// Forward declaration
class Entity;

namespace ModelInstantiation {
    // Model을 Entity로 변환하여 MeshComponent와 MaterialComponent를 추가합니다.
    // mergeAllMeshes: true면 모든 메시를 하나의 MeshComponent로 병합, false면 각 메시마다 별도 MeshComponent 생성
    // 반환값: 성공 시 true, 실패 시 false
    //
    // 사용 예제:
    //   FBXLoader loader;
    //   Model* model = loader.Load("Models/myModel.fbx");
    //   if (model) {
    //       auto entity = std::make_unique<Entity>(id);
    //       entity->AddComponent<TransformComponent>();
    //       ModelInstantiation::InstantiateToEntity(model, entity.get(), true);
    //       renderSystem->RegisterEntity(entity.get());
    //       delete model; // 메모리 관리 필요
    //   }
    bool InstantiateToEntity(const Model* model, Entity* entity, bool mergeAllMeshes = true);
}


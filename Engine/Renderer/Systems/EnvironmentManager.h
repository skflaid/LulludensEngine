#pragma once
#include "Core/TextureManager.h"

class Entity;
struct SkyComponent;

class EnvironmentManager
{
public:
    void Initialize(TextureManager* textureManager);

    void SetActiveSky(Entity* skyEntity);
    Entity* GetActiveSkyEntity() const;
    SkyComponent* GetActiveSkyComponent() const;
    TextureInfo* GetActiveSkyCubemap() const;

private:
    TextureManager* m_TextureManager = nullptr;
    Entity* m_ActiveSkyEntity = nullptr;
};

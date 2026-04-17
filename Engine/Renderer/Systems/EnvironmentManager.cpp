#include "EnvironmentManager.h"
#include "Core/Entity.h"
#include "Renderer/Components/SkyComponent.h"

void EnvironmentManager::Initialize(TextureManager* textureManager)
{
    m_TextureManager = textureManager;
}

void EnvironmentManager::SetActiveSky(Entity* skyEntity)
{
    m_ActiveSkyEntity = skyEntity;
}

Entity* EnvironmentManager::GetActiveSkyEntity() const
{
    return m_ActiveSkyEntity;
}

SkyComponent* EnvironmentManager::GetActiveSkyComponent() const
{
    if (!m_ActiveSkyEntity) {
        return nullptr;
    }

    return m_ActiveSkyEntity->GetComponent<SkyComponent>();
}

TextureInfo* EnvironmentManager::GetActiveSkyCubemap() const
{
    SkyComponent* sky = GetActiveSkyComponent();
    if (!sky || !m_TextureManager || sky->CubemapName.empty()) {
        return nullptr;
    }

    TextureInfo* texture = m_TextureManager->GetTexture(sky->CubemapName);
    if (!texture || !texture->IsValid || !texture->IsCubeMap) {
        return nullptr;
    }

    return texture;
}

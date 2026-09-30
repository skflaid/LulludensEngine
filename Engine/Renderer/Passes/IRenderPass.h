#pragma once

#include <d3d12.h>
#include <string_view>
#include "Renderer/Graph/RenderResourceUsage.h"

struct RenderPassContext
{
	UINT frameIndex = 0;
	ID3D12GraphicsCommandList* commandList = nullptr;
};

class IRenderPass
{
public:
	virtual ~IRenderPass() = default;

	virtual std::string_view GetName() const noexcept = 0;

	virtual const std::vector<ResourceUsage>& GetResourceUsages() const noexcept = 0;

	virtual void Execute(const RenderPassContext& context) = 0;
};
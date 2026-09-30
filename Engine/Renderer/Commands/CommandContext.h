#pragma once

#include "Renderer/Resources/GpuResource.h"

#include <d3d12.h>
#include <vector>

class CommandContext
{
public:
	CommandContext();
	
	void Begin(
		ID3D12GraphicsCommandList* commandList);

	void TransitionResource(
		GpuResource& resource,
		D3D12_RESOURCE_STATES requiredState);
	
	void FlushResourceBarriers();

private:
	ID3D12GraphicsCommandList* m_CommandList = nullptr;

	std::vector<D3D12_RESOURCE_BARRIER> m_PendingBarriers;
};
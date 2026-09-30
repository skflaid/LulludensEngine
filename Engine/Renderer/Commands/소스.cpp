#include "CommandContext.h"

#include <cassert>

CommandContext::CommandContext()
{
	m_PendingBarriers.reserve(16);
}

void CommandContext::Begin(
	ID3D12GraphicsCommandList* commandList)
{
	assert(commandList != nullptr);
	assert(m_PendingBarriers.empty());

	m_CommandList = commandList;
}

void CommandContext::TransitionResource(
	GpuResource& resource,
	D3D12_RESOURCE_STATES requiredState)
{
	assert(m_CommandList != nullptr);
	assert(resource.IsValid());

	const D3D12_RESOURCE_STATES currentState = resource.GetCurrentState();
	
	if (currentState == requiredState)
		return;

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

	barrier.Transition.pResource = resource.Get();
	barrier.Transition.StateBefore = currentState;
	barrier.Transition.StateAfter = requiredState;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

	m_PendingBarriers.push_back(barrier);

	// Flush 전이라도 논리적으로는 다음 명령이 새 상태를 사용
	resource.SetCurrentState(requiredState);
}

void CommandContext::FlushResourceBarriers()
{
	assert(m_CommandList != nullptr);

	if (m_PendingBarriers.empty())
		return;

	m_CommandList->ResourceBarrier(
		static_cast<UINT>(m_PendingBarriers.size()),
		m_PendingBarriers.data());

	m_PendingBarriers.clear();
}
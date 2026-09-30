#pragma once

#include "IRenderPass.h"

#include <functional>
#include <string>
#include <utility>

class FunctionRenderPass final : public IRenderPass
{
public:
	using ExecuteFunction = std::function<void(const RenderPassContext&)>;

	FunctionRenderPass(
		std::string name,
		std::vector<ResourceUsage> resourceUsages,
		ExecuteFunction executeFunction)
		: m_Name(std::move(name))
		, m_ResourceUsages(std::move(resourceUsages))
		, m_ExecuteFunction(std::move(executeFunction))
	{
	}

	std::string_view GetName() const noexcept override
	{
		return m_Name;
	}

	const std::vector<ResourceUsage>& GetResourceUsages() const noexcept override
	{
		return m_ResourceUsages;
	}

	void Execute(const RenderPassContext& context) override
	{
		if (m_ExecuteFunction)
			m_ExecuteFunction(context);
	}

private:
	std::string m_Name;
	std::vector<ResourceUsage> m_ResourceUsages;
	ExecuteFunction m_ExecuteFunction;
};
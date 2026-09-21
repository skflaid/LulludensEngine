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
		ExecuteFunction executeFunction)
		: m_Name(std::move(name))
		, m_ExecuteFunction(std::move(executeFunction))
	{
	}

	std::string_view GetName() const noexcept override
	{
		return m_Name;
	}

	void Execute(const RenderPassContext& context) override
	{
		m_ExecuteFunction(context);
	}

private:
	std::string m_Name;
	ExecuteFunction m_ExecuteFunction;
};
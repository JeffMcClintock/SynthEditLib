#pragma once
#include "ViewBase.h"

namespace SE2
{
	// The one top-level view.
	class ContainerViewStruct : public TopView
	{
	public:
		ContainerViewStruct(gmpi::drawing::Size size) : TopView(size)
		{
		}

		int getViewType() override
		{
			return CF_STRUCTURE_VIEW;
		}

		std::string getSkinName() override
		{
			return "default3";
		}
		void BuildModules(Json::Value* context, std::map<int, class ModuleView*>& guiObjectMap) override;
		ConnectorViewBase* createCable(CableType type, int32_t handleFrom, int32_t fromPin) override;
		gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override;
	};
}

#pragma once
#include <vector>
#include <memory>
#include "ViewBase.h"
#include "../se_sdk3_hosting/GraphicsRedrawClient.h"

namespace SE2
{
	class IPresenter;
}

namespace SE2
{
	// The one top-level view.
	class ContainerViewPanel : public TopView
	{
	public:
		ContainerViewPanel(gmpi::drawing::Size size) : TopView(size)
		{
		}

		int getViewType() override
		{
			return CF_PANEL_VIEW;
		}

		void Refresh(Json::Value* context, std::map<int, SE2::ModuleView*>& guiObjectMap_) override;
		void BuildModules(Json::Value* context, std::map<int, class ModuleView*>& guiObjectMap) override;
		ConnectorViewBase* createCable(CableType type, int32_t handleFrom, int32_t fromPin) override;
		gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override;
	};
}

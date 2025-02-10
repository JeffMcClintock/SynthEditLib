#pragma once
#include <vector>
#include <memory>
#include "ViewBase.h"
//#include "./DrawingFrame_win32.h"
#include "../se_sdk3_hosting/GraphicsRedrawClient.h"

namespace SE2
{
	class IPresenter;
}

namespace SE2
{
	// The one top-level view.
	class ContainerViewPanel : public ViewBase
	{
		std::string skinName_;

	public:
		ContainerViewPanel(GmpiDrawing::Size size) : ViewBase(size)
		{
		}

		int getViewType() override
		{
			return CF_PANEL_VIEW;
		}
		std::string getSkinName() override
		{
			return skinName_;
		}

		void Refresh(Json::Value* context, std::map<int, SE2::ModuleView*>& guiObjectMap_) override;
		void BuildModules(Json::Value* context, std::map<int, class ModuleView*>& guiObjectMap) override;
		int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override;
	};
}

#pragma once
#include "ViewBase.h"
//#include <vector>
//#include <memory>
//#include "./DrawingFrame_win32.h"

namespace SE2
{
	class IPresenter;
}

namespace SE2
{
	// The one top-level view.
	class ContainerViewStruct : public ViewBase
	{
	public:
		ContainerViewStruct(GmpiDrawing::Size size) : ViewBase(size)
		{
		}

		int getViewType() override
		{
			return CF_STRUCTURE_VIEW;
		}

		std::string getSkinName() override
		{
			return "default2";
		}
		void BuildModules(Json::Value* context, std::map<int, class ModuleView*>& guiObjectMap) override;
		ConnectorViewBase* createCable(CableType type, int32_t handleFrom, int32_t fromPin) override;
		int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override;
	};
}

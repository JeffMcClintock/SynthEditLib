#pragma once
#include <vector>
#include <memory>
#include "ViewBase.h"
#include "./DrawingFrame_win32.h"
#include "../se_sdk3_hosting/GraphicsRedrawClient.h"

namespace SE2
{
	class IPresenter;
}

namespace SE2
{
	// The one top-level view.
	class ContainerView : public ViewBase, public IGraphicsRedrawClient, public gmpi_gui_api::IMpKeyClient
	{
		std::string skinName_;
		std::string draggingNewModuleId;
		std::unique_ptr<GmpiSdk::ContextMenuHelper::ContextMenuCallbacks> contextMenuCallbacks;

	public:
		ContainerView(GmpiDrawing::Size size);
		~ContainerView();

		void setDocument(SE2::IPresenter* presenter, int pviewType);

		int GetViewType()
		{
			return viewType;
		}

		IViewChild* Find(GmpiDrawing::Point& p);
		void Unload();
		std::string getSkinName() override
		{
			if( viewType == CF_PANEL_VIEW )
				return skinName_;
			else
				return "default2";
		}

		void Refresh(Json::Value* context, std::map<int, SE2::ModuleView*>& guiObjectMap_) override;

		// Inherited via IMpGraphics
		int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override;
		//int32_t onPointerDown(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
		int32_t onPointerUp(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;

		int32_t populateContextMenu(float /*x*/, float /*y*/, gmpi::IMpUnknown* /*contextMenuItemsSink*/) override;
		int32_t onContextMenu(int32_t /*selection*/) override;

		GmpiDrawing::Point MapPointToView(ViewBase* parentView, GmpiDrawing::Point p) override
		{
			return p;
		}

		bool isShown() override
		{
			return true;
		}
		
		void OnPatchCablesVisibilityUpdate() override;

		void PreGraphicsRedraw() override;

		int32_t OnKeyPress(wchar_t c) override;
		void DragNewModule(const char* id);

		int32_t queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
		{
			*returnInterface = nullptr;

			if (iid == IGraphicsRedrawClient::guid)
			{
				*returnInterface = static_cast<IGraphicsRedrawClient*>(this);
				addRef();
				return gmpi::MP_OK;
			}

			if (iid == gmpi_gui_api::IMpKeyClient::guid)
			{
				*returnInterface = static_cast<gmpi_gui_api::IMpKeyClient*>(this);
				addRef();
				return gmpi::MP_OK;
			}

			return ViewBase::queryInterface(iid, returnInterface);
		}
		GMPI_REFCOUNT;
	};
}

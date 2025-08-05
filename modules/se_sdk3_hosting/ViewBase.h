#pragma once
#include <vector>
#include <memory>
#include "IViewChild.h"
#include "xplatform.h"
#include "Drawing.h"
#include "UgDatabase.h"
#include "mp_gui.h"
#include "Presenter.h"
#include "../se_sdk3_hosting/GraphicsRedrawClient.h"
#include "GmpiApiDrawing.h"

namespace GmpiGuiHosting
{
	class ConnectorViewBase;
}

class IGuiHost2;
struct DrawingFrameBase2;

namespace SE2
{
	class DragLine;
	class ConnectorViewBase;

	// Base of any view that displays modules. Itself behaving as a standard graphics module.
	class ViewBase : public gmpi_gui::MpGuiGfxBase, public IGraphicsRedrawClient, public gmpi_gui_api::IMpKeyClient
	{
		friend class ResizeAdorner;
		friend class ViewChild;
		
		GmpiDrawing::Point pointPrev;
		GmpiDrawing_API::MP1_POINT lastMovePoint = { -1, -1 };

	protected:
bool isIteratingChildren = false;
		std::string draggingNewModuleId;
		std::unique_ptr<GmpiSdk::ContextMenuHelper::ContextMenuCallbacks> contextMenuCallbacks;
		bool isArranged = false;
		bool childrenDirty = false;
		std::vector< std::unique_ptr<IViewChild> > children;
		std::vector<ModuleView*> children_monodirectional;
		std::unique_ptr<IPresenter> presenter;

		GmpiDrawing::Rect drawingBounds;
		IViewChild* mouseCaptureObject = {};
		IViewChild* mouseOverObject = {};
		IViewChild* modulePicker = {};

		bool isDraggingModules = false;
		GmpiDrawing::Size DraggingModulesOffset = {};
		GmpiDrawing::Point DraggingModulesInitialTopLeft = {};

#ifdef _WIN32
		DrawingFrameBase2* frameWindow = {};
#endif
		class ModuleViewPanel* patchAutomatorWrapper_ = {};

		void ConnectModules(const Json::Value& element, std::map<int, class ModuleView*>& guiObjectMap);// , ModuleView* patchAutomatorWrapper);
		class ModuleViewPanel* getPatchAutomator(std::map<int, class ModuleView*>& guiObjectMap);
		void PreGraphicsRedraw() override;
		void processUnidirectionalModules();

	public:
		ViewBase(GmpiDrawing::Size size);
		virtual ~ViewBase() { mouseOverObject = {}; }

		void setDocument(SE2::IPresenter* presenter);
		int32_t setHost(gmpi::IMpUnknown* host) override;

		void Init(class IPresenter* ppresentor);
		void BuildPatchCableNotifier(std::map<int, class ModuleView*>& guiObjectMap);
		virtual void BuildModules(Json::Value* context, std::map<int, class ModuleView*>& guiObjectMap) = 0;
		void initMonoDirectionalModules(std::map<int, SE2::ModuleView*>& guiObjectMap);

		virtual int getViewType() = 0;

		void OnChildResize(IViewChild* child);
		void RemoveChild(IViewChild* child);
		virtual void markDirtyChild(IViewChild* child);

		int32_t measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE * returnDesiredSize) override;
		int32_t arrange(GmpiDrawing_API::MP1_RECT finalRect) override;

		int32_t onPointerDown(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
		int32_t onPointerMove(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
		int32_t onPointerUp(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
		int32_t onMouseWheel(int32_t flags, int32_t delta, GmpiDrawing_API::MP1_POINT point) override;
		int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override;
		int32_t setHover(bool isMouseOverMe) override;

		void calcMouseOverObject(int32_t flags);
		void OnChildDeleted(IViewChild* childObject);
		void onSubPanelMadeVisible();

		int32_t populateContextMenu(float /*x*/, float /*y*/, gmpi::IMpUnknown* /*contextMenuItemsSink*/) override;
		int32_t onContextMenu(int32_t idx) override;

		GmpiDrawing_API::IMpFactory* GetDrawingFactory()
		{
			GmpiDrawing_API::IMpFactory* temp = nullptr;
			getGuiHost()->GetDrawingFactory(&temp);
			return temp;
		}
		gmpi::ReturnCode getDrawingFactory(gmpi::api::IUnknown** returnFactory);

//		std::string getToolTip(GmpiDrawing_API::MP1_POINT point);
//		int32_t getToolTip(float x, float y, gmpi::IMpUnknown* returnToolTipString) override;
		int32_t getToolTip(GmpiDrawing_API::MP1_POINT point, gmpi::IString* returnString) override;

		virtual std::string getSkinName() = 0;

		virtual int32_t setCapture(IViewChild* module);
		bool isCaptured(IViewChild* module)
		{
			return mouseCaptureObject == module;
		}
		int32_t releaseCapture();
		//bool isMouseCaptured()
		//{
		//	return mouseCaptureObject != nullptr;
		//}

		virtual int32_t StartCableDrag(IViewChild* fromModule, int fromPin, GmpiDrawing::Point dragStartPoint, bool isHeldAlt, CableType type = CableType::PatchCable);
		void OnCableMove(ConnectorViewBase * dragline);
		int32_t EndCableDrag(GmpiDrawing_API::MP1_POINT point, ConnectorViewBase* dragline);
		void OnPatchCablesUpdate(RawView patchCablesRaw);
		void UpdateCablesBounds();
		void RemoveCables(ConnectorViewBase* cable);
		void RemoveModule(int32_t handle);

		void OnChangedChildHighlight(int phandle, int flags);

		void OnChildDspMessage(void * msg);

		void MoveToFront(IViewChild* child);
		void MoveToBack(IViewChild* child);

		SE2::IPresenter* Presenter()
		{
			return presenter.get();
		}

		void OnChangedChildSelected(int handle, bool selected);
		void OnChangedChildPosition(int phandle, GmpiDrawing::Rect& newRect);
		void OnChangedChildNodes(int phandle, std::vector<GmpiDrawing::Point>& nodes);

		void OnDragSelectionBox(int32_t flags, GmpiDrawing::Rect selectionRect);

		// not to be confused with MpGuiGfxBase::invalidateRect
		virtual void ChildInvalidateRect(const GmpiDrawing_API::MP1_RECT& invalidRect)
		{
			getGuiHost()->invalidateRect(&invalidRect);
		}
		virtual void OnChildMoved() {}
		virtual int32_t ChildCreatePlatformTextEdit(const GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformText** returnTextEdit)
		{
			return getGuiHost()->createPlatformTextEdit(const_cast<GmpiDrawing_API::MP1_RECT*>(rect), returnTextEdit);
		}
		virtual int32_t ChildCreatePlatformMenu(const GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformMenu** returnMenu)
		{
			return getGuiHost()->createPlatformMenu(const_cast<GmpiDrawing_API::MP1_RECT*>(rect), returnMenu);
		}

		void autoScrollStart();
		void autoScrollStop();
		void DoClose();

		IViewChild* Find(GmpiDrawing::Point& p);
		void Unload();
		virtual void Refresh(Json::Value* context, std::map<int, SE2::ModuleView*>& guiObjectMap_);

		virtual GmpiDrawing::Point MapPointToView(ViewBase* parentView, GmpiDrawing::Point p)
		{
			return p;
		}

		virtual bool isShown()
		{
			return true;
		}

		virtual void OnPatchCablesVisibilityUpdate();

		// gmpi_gui_api::IMpKeyClient
		int32_t OnKeyPress(wchar_t c) override;

		gmpi::ReturnCode onKey(int32_t key, gmpi::drawing::Point* pointerPosOrNull);

		bool DoModulePicker(gmpi::drawing::Point currentPointerPos);
		void DismissModulePicker();
		void DragNewModule(const char* id);
		virtual ConnectorViewBase* createCable(CableType type, int32_t handleFrom, int32_t fromPin) = 0;

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

			return gmpi_gui::MpGuiGfxBase::queryInterface(iid, returnInterface);
		}
		GMPI_REFCOUNT
	};

	class SelectionDragBox : public ViewChild
	{
		GmpiDrawing::Point startPoint;

	public:
		SelectionDragBox(ViewBase* pParent, GmpiDrawing_API::MP1_POINT point) :
			ViewChild(pParent, -1)
		{
			startPoint = point;
			arrange(GmpiDrawing::Rect(point.x, point.y, point.x, point.y));
			parent->setCapture(this);
			parent->invalidateRect(&bounds_);
		}

		static const int lineWidth_ = 3;

		// IViewChild
		int32_t onPointerDown(int32_t flags, GmpiDrawing_API::MP1_POINT point) override
		{
			/*
			bounds_.left = point.x - 1;
			bounds_.top = point.y - 1;
			bounds_.right = point.x;
			bounds_.bottom = point.y;

			parent->invalidateRect(&bounds_);
			*/
			return gmpi::MP_OK;
		}
		int32_t onPointerMove(int32_t flags, GmpiDrawing_API::MP1_POINT point) override
		{
			GmpiDrawing::Rect invalidRect(bounds_);

			bounds_.right = (std::max)(startPoint.x, point.x);
			bounds_.left = (std::min)(startPoint.x, point.x);
			bounds_.bottom = (std::max)(startPoint.y, point.y);
			bounds_.top = (std::min)(startPoint.y, point.y);

			invalidRect.Union(bounds_);
			invalidRect.Inflate(2);

			parent->invalidateRect(&invalidRect);

			return gmpi::MP_OK;
		}

		int32_t onPointerUp(int32_t flags, GmpiDrawing_API::MP1_POINT point) override
		{
			parent->releaseCapture();
			parent->autoScrollStop();

			GmpiDrawing::Rect invalidRect(bounds_);
			invalidRect.Inflate(2);
			parent->invalidateRect(&invalidRect);

			// creat local copy of these to use after 'this' is deleted.
			auto localParent = parent;
			auto localBounds = bounds_;

			localParent->RemoveChild(this);
			// 'this' now deleted!!!

			// carefull not to access 'this'
			const float smallDragSuppression = 2.f;
			if (localBounds.getWidth() > smallDragSuppression && localBounds.getHeight() > smallDragSuppression)
			{
				localParent->OnDragSelectionBox(flags, localBounds);
			}

			return gmpi::MP_OK;
		}

		int32_t onMouseWheel(int32_t flags, int32_t delta, GmpiDrawing_API::MP1_POINT point) override
		{
			return gmpi::MP_UNHANDLED;
		}

		void OnRender(GmpiDrawing::Graphics& g) override
		{
			GmpiDrawing::Color col{ GmpiDrawing::Color::SkyBlue };
			col.a = 0.3f;
			auto brush = g.CreateSolidColorBrush(col);
			auto r = bounds_ - GmpiDrawing::Size(bounds_.left, bounds_.top);
			g.FillRectangle(r, brush);
			brush.SetColor(GmpiDrawing::Color::White);
			g.DrawRectangle(r, brush);
		}

		int32_t populateContextMenu(float /*x*/, float /*y*/, gmpi::IMpUnknown* /*contextMenuItemsSink*/) override
		{
			return 0;
		}
		int32_t onContextMenu(int32_t idx) override
		{
			return 0;
		}
		void OnMoved(GmpiDrawing::Rect& newRect) override {}
		void OnNodesMoved(std::vector<GmpiDrawing::Point>& newNodes) override {}
		GmpiDrawing::Point getConnectionPoint(CableType cableType, int pinIndex) override
		{
			return GmpiDrawing::Point();
		}
	};
} //namespace SE2


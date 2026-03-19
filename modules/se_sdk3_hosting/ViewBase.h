#pragma once
#include <vector>
#include <memory>
#include "IViewChild.h"
//#include "xplatform.h"
#include "GmpiUiDrawing.h"
#include "helpers/GmpiPluginEditor.h"
#include "Presenter.h"
//#include "../se_sdk3_hosting/GraphicsRedrawClient.h"

#include "helpers/Timer.h"
#include "helpers/NativeUi.h"
#include "../se_sdk3/Drawing_API.h"

namespace GmpiGuiHosting
{
	class ConnectorViewBase;
}

class IGuiHost2;
struct DrawingFrameBase2;

namespace SE2
{
	class ConnectorViewBase;

	struct scrollBarSpec
	{
		double Value;
		double Minimum;
		double Maximum;
		double LargeChange;
		double SmallChange;
		double ViewportSize;
	};

	// Base of any view that displays modules. Itself behaving as a standard-ish graphics module.
	class ViewBase :
		public gmpi::editor::PluginEditor
//		public gmpi_gui::MpGuiGfxBase
//		, public legacy::IGraphicsRedrawClient
		, public gmpi::TimerClient
	{
		friend class ResizeAdorner;
		friend class ViewChild;
		
		gmpi::drawing::Point pointPrev;
		gmpi::drawing::Point lastMovePoint = { -1, -1 };
		gmpi::drawing::Point currentPointerPosAbsolute = { -1, -1 };

	protected:
bool isIteratingChildren = false;
		std::string draggingNewModuleId;
		bool isArranged = false;
		bool childrenDirty = false;
		std::vector< std::unique_ptr<IViewChild> > children;
		std::vector<ModuleView*> children_monodirectional;
		std::unique_ptr<IPresenter> presenter;

		gmpi::drawing::Rect drawingBounds;
		IViewChild* mouseCaptureObject = {};
		IViewChild* mouseOverObject = {};
		IViewChild* modulePicker = {};

		bool isDraggingModules = false;
		IViewChild* DraggingObject = {};
		gmpi::drawing::Size DraggingModulesOffset = {};
		gmpi::drawing::Point DraggingModulesInitialTopLeft = {};

		// pan and zoom
		gmpi::drawing::Size scrollPos = {};
		float zoomFactor = 1.0f;
		gmpi::drawing::Matrix3x2 viewTransform;
		gmpi::drawing::Matrix3x2 inv_viewTransform;
		bool avoidRecusion{}; // from scroll bars
		bool isAutoScrolling = false;

		//gmpi::api::IDrawingHost* drawingHost_ = {};
		//gmpi::api::IInputHost* inputHost_ = {};

		void calcViewTransform();
		bool onTimer() override;

#ifdef _WIN32
		DrawingFrameBase2* frameWindow = {};
#endif
		class ModuleViewPanel* patchAutomatorWrapper_ = {};

		void ConnectModules(const Json::Value& element, std::map<int, class ModuleView*>& guiObjectMap);// , ModuleView* patchAutomatorWrapper);
		class ModuleViewPanel* getPatchAutomator(std::map<int, class ModuleView*>& guiObjectMap);
// TODO		void preGraphicsRedraw() override;
		void processUnidirectionalModules();

	public:
		ViewBase(gmpi::drawing::Size size);
		virtual ~ViewBase()
		{
			mouseOverObject = {};
		}

		void setDocument(SE2::IPresenter* presenter);

		void Init(class IPresenter* ppresentor);
		void BuildPatchCableNotifier(std::map<int, class ModuleView*>& guiObjectMap);
		virtual void BuildModules(Json::Value* context, std::map<int, class ModuleView*>& guiObjectMap) = 0;
		void initMonoDirectionalModules(std::map<int, SE2::ModuleView*>& guiObjectMap);

		virtual int getViewType() = 0;

		void OnChildResize(IViewChild* child);
		void RemoveChild(IViewChild* child);
		virtual void markDirtyChild(IViewChild* child);
//		gmpi::ReturnCode populateContextMenu2(gmpi::api::IContextItemSink* menu, gmpi::drawing::Point point);

		gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override;
#if 0 // OLD: handled by base class, to be removed.
		// gmpi::api::IDrawingClient
		gmpi::ReturnCode open(gmpi::api::IUnknown* host) override;
		gmpi::ReturnCode getClipArea(gmpi::drawing::Rect* returnRect) override;
#endif
		gmpi::ReturnCode measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize) override;
		gmpi::ReturnCode arrange(const gmpi::drawing::Rect* finalRect) override;

		// gmpi::api::IInputClient
		gmpi::ReturnCode setHover(bool isMouseOverMe) override;
		gmpi::ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override;
		gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta) override;
		gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink) override;
		gmpi::ReturnCode onContextMenu(int32_t idx) override;
		gmpi::ReturnCode onKeyPress(wchar_t c) override;

#if 0 // old stuff, to be removed
		void measure(gmpi::drawing::Size availableSize, gmpi::drawing::Size* returnDesiredSize);
		void arrange(gmpi::drawing::Rect finalRect);
		virtual int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext);
		int32_t onPointerDownLegacy(int32_t flags, gmpi::drawing::Point point);
		int32_t onPointerMoveLegacy(int32_t flags, gmpi::drawing::Point point);
		int32_t onPointerUpLegacy(int32_t flags, gmpi::drawing::Point point);
		int32_t onMouseWheelLegacy(int32_t flags, int32_t delta, gmpi::drawing::Point point);
		int32_t populateContextMenu(float x, float y, gmpi::api::IUnknown* contextMenuItemsSink);
		int32_t getToolTip(gmpi::drawing::Point point, gmpi::api::IString* returnString);
		void preGraphicsRedraw();
#endif

		// notification to scrollbars
		std::function<void(const scrollBarSpec&)> hscrollBar;
		std::function<void(const scrollBarSpec&)> vscrollBar;

		// notificate *from* scrollbars or document
		void setZoomFactor(float newZoomFactor)
		{
			zoomFactor = newZoomFactor;
		}
		void onHScroll(double newValue);
		void onVScroll(double newValue);
		void updateScrollBars();
		void autoScrollStart();
		void autoScrollStop();

		void calcMouseOverObject(int32_t flags);
		void OnChildDeleted(IViewChild* childObject);
		void onSubPanelMadeVisible();

// TODO		int32_t populateContextMenu(float /*x*/, float /*y*/, gmpi::api::IUnknown* /*contextMenuItemsSink*/) override;

		GmpiDrawing_API::IMpFactory* GetDrawingFactory()
		{
			GmpiDrawing_API::IMpFactory* temp = nullptr;
			if (drawingHost)
				drawingHost->getDrawingFactory(reinterpret_cast<gmpi::api::IUnknown**>(&temp));
			return temp;
		}
		gmpi::ReturnCode getDrawingFactory(gmpi::api::IUnknown** returnFactory);

// TODO		int32_t getToolTip(gmpi::drawing::Point point, gmpi::IString* returnString) override;

		virtual std::string getSkinName() = 0;

		virtual int32_t setCapture(IViewChild* module);
		bool isCaptured(IViewChild* module)
		{
			return mouseCaptureObject == module;
		}
		int32_t releaseCapture();

		virtual int32_t StartCableDrag(IViewChild* fromModule, int fromPin, gmpi::drawing::Point dragStartPoint, gmpi::drawing::Point mousePoint);
		bool OnCableMove(ConnectorViewBase * dragline);
		bool EndCableDrag(gmpi::drawing::Point point, ConnectorViewBase* dragline, int32_t keyFlags);
		void OnPatchCablesUpdate(RawView patchCablesRaw);
		void UpdateCablesBounds();
		void RemoveCables(ConnectorViewBase* cable);
		void RemoveModule(int32_t handle);
		std::pair<ConnectorViewBase*, int> getTopCable(int32_t handle, int32_t pinIdx);

		void OnChangedChildHighlight(int phandle, int flags);

		void OnChildDspMessage(void * msg);

		void MoveToFront(IViewChild* child);
		void MoveToBack(IViewChild* child);

		SE2::IPresenter* Presenter()
		{
			return presenter.get();
		}

		void OnChangedChildSelected(int handle, bool selected);
		void OnChangedChildPosition(int phandle, gmpi::drawing::Rect& newRect);
		void OnChangedChildNodes(int phandle, std::vector<gmpi::drawing::Point>& nodes);

		void OnDragSelectionBox(int32_t flags, gmpi::drawing::Rect selectionRect);

		// not to be confused with MpGuiGfxBase::invalidateRect
		virtual void ChildInvalidateRect(const gmpi::drawing::Rect& invalidRect)
		{
			const auto scrolledRect = transformRect(viewTransform, invalidRect);
			invalidateRect(&scrolledRect);
		}
		void invalidateRect(const gmpi::drawing::Rect* invalidRect = {});
		virtual void OnChildMoved() {}
		/* todo
		virtual int32_t ChildCreatePlatformTextEdit(const gmpi::drawing::Rect* rect, gmpi_gui::IMpPlatformText** returnTextEdit)
		{
			return getGuiHost()->createPlatformTextEdit(const_cast<gmpi::drawing::Rect*>(rect), returnTextEdit);
		}
		virtual int32_t ChildCreatePlatformMenu(const gmpi::drawing::Rect* rect, gmpi_gui::IMpPlatformMenu** returnMenu)
		{
			return getGuiHost()->createPlatformMenu(const_cast<gmpi::drawing::Rect*>(rect), returnMenu);
		}
		*/

		void DoClose();

		IViewChild* Find(gmpi::drawing::Point& p);
		void Unload();
		virtual void Refresh(Json::Value* context, std::map<int, SE2::ModuleView*>& guiObjectMap_);

		virtual gmpi::drawing::Point MapPointToView(ViewBase* parentView, gmpi::drawing::Point p)
		{
			return p;
		}

		// Get the compound transform matrix from this view to the topmost view (including zoom and pan)
		virtual gmpi::drawing::Matrix3x2 GetTransformToTopView()
		{
			// Top-level view returns its zoom/pan transform
			return viewTransform;
		}

		virtual bool isShown()
		{
			return true;
		}

		virtual void OnPatchCablesVisibilityUpdate();

		// gmpi_gui_api::IMpKeyClient
		int32_t OnKeyPress(wchar_t c);

		gmpi::ReturnCode onKey(int32_t key, gmpi::drawing::Point* pointerPosOrNull);

		bool DoModulePicker(gmpi::drawing::Point currentPointerPos);
		void DismissModulePicker();
		void DragNewModule(const char* id);
		virtual ConnectorViewBase* createCable(CableType type, int32_t handleFrom, int32_t fromPin) = 0;

		/* handled by PluginEditor base class
		gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
		{
			*returnInterface = {};
			GMPI_QUERYINTERFACE(gmpi::api::IDrawingClient)
			GMPI_QUERYINTERFACE(gmpi::api::IInputClient)
			return gmpi::ReturnCode::NoSupport;
		}
		*/
		GMPI_REFCOUNT
	};

	class TopView : public ViewBase
	{
	protected:
		std::string skinName_;

	public:
		TopView(gmpi::drawing::Size size) : ViewBase(size)
		{
		}

		std::string getSkinName() override
		{
			return skinName_;
		}
	};

	class SelectionDragBox : public ViewChild
	{
		gmpi::drawing::Point startPoint;

	public:
		SelectionDragBox(ViewBase* pParent, gmpi::drawing::Point point) :
			ViewChild(pParent, -1)
		{
			startPoint = point;
			arrange({ point.x, point.y, point.x, point.y });
			parent->setCapture(this);
			parent->invalidateRect(&bounds_);
		}

		static const int lineWidth_ = 3;

		// IViewChild
		gmpi::ReturnCode setHover(bool isMouseOverMe) override
		{
			return gmpi::ReturnCode::Unhandled;
		}

		gmpi::ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override
		{
			return gmpi::ReturnCode::Unhandled;
		}

		gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override
		{
			/*
			bounds_.left = point.x - 1;
			bounds_.top = point.y - 1;
			bounds_.right = point.x;
			bounds_.bottom = point.y;

			parent->invalidateRect(&bounds_);
			*/
			return gmpi::ReturnCode::Unhandled;
		}
		gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override
		{
			gmpi::drawing::Rect invalidRect(bounds_);

			bounds_.right = (std::max)(startPoint.x, point.x);
			bounds_.left = (std::min)(startPoint.x, point.x);
			bounds_.bottom = (std::max)(startPoint.y, point.y);
			bounds_.top = (std::min)(startPoint.y, point.y);

			invalidRect = unionRect(invalidRect, bounds_);
			invalidRect = inflateRect(invalidRect, 2);

			parent->invalidateRect(&invalidRect);
			return gmpi::ReturnCode::Unhandled;
		}

		gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override
		{
			parent->releaseCapture();
			parent->autoScrollStop();

			const auto invalidRect = inflateRect(bounds_, 2);
			parent->invalidateRect(&invalidRect);

			// creat local copy of these to use after 'this' is deleted.
			auto localParent = parent;
			auto localBounds = bounds_;

			localParent->RemoveChild(this);
			// 'this' now deleted!!!

			// carefull not to access 'this'
			const float smallDragSuppression = 2.f;
			if (getWidth(localBounds) > smallDragSuppression && getHeight(localBounds) > smallDragSuppression)
			{
				localParent->OnDragSelectionBox(flags, localBounds);
			}

			return gmpi::ReturnCode::Unhandled;
		}

		gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta) override
		{
			return gmpi::ReturnCode::Unhandled;
		}

		void render(gmpi::drawing::Graphics& g) override
		{
			gmpi::drawing::Color col{ gmpi::drawing::Colors::SkyBlue };
			col.a = 0.3f;
			auto brush = g.createSolidColorBrush(col);
			gmpi::drawing::Rect r{ 0.f, 0.f, getWidth(bounds_), getHeight(bounds_) };
			g.fillRectangle(r, brush);
			brush.setColor(gmpi::drawing::Colors::White);
			g.drawRectangle(r, brush);
		}

		gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink) override
		{
			return gmpi::ReturnCode::Unhandled;
		}
		gmpi::ReturnCode onContextMenu(int32_t idx) override
		{
			return gmpi::ReturnCode::Unhandled;
		}
		gmpi::ReturnCode onKeyPress(wchar_t c) override
		{
			return gmpi::ReturnCode::Unhandled;
		}
		void OnMoved(gmpi::drawing::Rect& newRect) override {}
		void OnNodesMoved(std::vector<gmpi::drawing::Point>& newNodes) override {}
		gmpi::drawing::Point getConnectionPoint(CableType cableType, int pinIndex) override
		{
			return gmpi::drawing::Point();
		}
	};
} //namespace SE2


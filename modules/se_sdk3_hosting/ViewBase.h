#pragma once
#include <vector>
#include <memory>
#include "IViewChild.h"
#include "GmpiUiDrawing.h"
#include "helpers/GmpiPluginEditor.h"
#include "Presenter.h"

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

	constexpr int viewDimensions = 7968; // DIPs (divisible by grids 60x60 + 2 24 pixel borders)

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
		, public gmpi::TimerClient
	{
		friend class ResizeAdorner;
		friend class ViewChild;

	protected:
		gmpi::drawing::Point pointPrev;
		gmpi::drawing::Point lastMovePoint = { -1, -1 };
		gmpi::drawing::Point currentPointerPosAbsolute = { -1, -1 };

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

		// Transform from this view's child-local coords to its parent's coords.
		// TopView derives it from pan/zoom (via calcViewTransform). SubView sets it
		// to a plain translation (its pan). Ordinary views leave it at identity.
		gmpi::drawing::Matrix3x2 viewTransform;          // quantized (pixel-snapped) — for rendering
		gmpi::drawing::Matrix3x2 viewTransformPrecise;    // unquantized — for coordinate mapping
		gmpi::drawing::Matrix3x2 inv_viewTransform;       // inverse of precise transform

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

		// called when a drag-to-create operation from the module browser ends (drop or cancel)
		std::function<void()> onDragNewModuleEnded;

		// Auto-scroll (during module drag / selection rect / cable drag) is a TopView
		// concern — it adjusts the TopView's centerPos on a timer. Keep these virtual
		// no-ops on ViewBase so in-repo callers that hold a ViewBase* (adorners, cables,
		// selection boxes etc.) can call parent->autoScrollStart/Stop() uniformly.
		virtual void autoScrollStart() {}
		virtual void autoScrollStop() {}

		void calcMouseOverObject(int32_t flags);
		void OnChildDeleted(IViewChild* childObject);
		void onSubPanelMadeVisible();

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

		GMPI_REFCOUNT
	};

	class TopView : public ViewBase
	{
	protected:
		std::string skinName_;

		// pan and zoom - ground truth is centerPos (document coords) + zoomFactor.
		// scrollPos is derived on demand in calcViewTransform() and must not be stored as state.
		gmpi::drawing::Point centerPos = {};
		float zoomFactor = 1.0f;
		bool avoidRecusion{}; // from scroll bars
		bool isAutoScrolling = false;

		void calcViewTransform();

	public:
		TopView(gmpi::drawing::Size size) : ViewBase(size)
		{
		}

		std::string getSkinName() override
		{
			return skinName_;
		}

		// ViewBase::arrange does the layout; TopView recomputes pan/zoom after because
		// calcViewTransform depends on drawingBounds (set by ViewBase::arrange).
		gmpi::ReturnCode arrange(const gmpi::drawing::Rect* finalRect) override;

		// Wheel = zoom (with Ctrl) or pan (plain/Shift); other cases delegate to base.
		gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta) override;

		// Auto-scroll during drag: adjusts centerPos on a timer tick.
		bool onTimer() override;
		void autoScrollStart() override;
		void autoScrollStop() override;

		// Scroll-bar hooks. External UI wires these up; internal code invokes them via
		// updateScrollBars() whenever pan/zoom changes.
		std::function<void(const scrollBarSpec&)> hscrollBar;
		std::function<void(const scrollBarSpec&)> vscrollBar;

		// Pan/zoom API. All four funnel through calcViewTransform() + updateScrollBars().
		void setZoomFactor(float newZoomFactor)
		{
			zoomFactor = newZoomFactor;
			calcViewTransform();
		}
		gmpi::drawing::Point getCenter() const
		{
			return centerPos;
		}
		void setCenter(gmpi::drawing::Point newCenter)
		{
			if (avoidRecusion)
				return;

			centerPos = newCenter;
			calcViewTransform();
			updateScrollBars();
		}
		void setPanZoom(gmpi::drawing::Point newCenter, float newZoomFactor)
		{
			if (avoidRecusion)
				return;

			centerPos = newCenter;
			zoomFactor = newZoomFactor;
			calcViewTransform();
			updateScrollBars();
		}
		// visibleLeft/visibleTop are in document coordinates (same as scrollbar Value)
		void onHScroll(double visibleLeft);
		void onVScroll(double visibleTop);
		void updateScrollBars();

		void renderGrid(gmpi::drawing::Graphics& g, gmpi::drawing::Color gridColor);
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
			parent->ChildInvalidateRect(bounds_);
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

			parent->ChildInvalidateRect(invalidRect);
			return gmpi::ReturnCode::Unhandled;
		}

		gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override
		{
			parent->releaseCapture();
			parent->autoScrollStop();

			const auto invalidRect = inflateRect(bounds_, 2);
			parent->ChildInvalidateRect(invalidRect);

			// creat local copy of these to use after 'this' is deleted.
			auto localParent = parent;
			auto localBounds = bounds_;

			localParent->RemoveChild(this);
			// 'this' now deleted!!!

			// carefull not to access 'this'
			const float smallDragSuppression = 2.f;
			if (getWidth(localBounds) > smallDragSuppression && getHeight(localBounds) > smallDragSuppression)
				localParent->OnDragSelectionBox(flags, localBounds);

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


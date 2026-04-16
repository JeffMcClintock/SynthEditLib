#pragma once
#include "./ModuleView.h"
#include "./ViewBase.h"

namespace SE2
{
	class IPresenter;
	class IViewChild;
	class ConnectorViewBase;
}

// sub-view shown on Panel.
class SubView : public SE2::ViewBase, public ISubView
{
	gmpi::editor::Pin<bool> showControlsLegacy;
	gmpi::editor::Pin<bool> showControlsOnModule_unused;
	gmpi::editor::Pin<bool> showControls;

	gmpi::drawing::Rect viewBounds;
	int parentViewType = 0;
	SE2::ModuleView* parent = {};

public:
	gmpi::drawing::Size offset_; // offset of children relative to bounds (not parent).

	SubView() : SE2::ViewBase({ 1000, 1000 })
	{
		init(showControlsLegacy);
		init(showControlsOnModule_unused);
		init(showControls);
	}
	SubView(SE2::ModuleView* parent, int pparentViewType = CF_PANEL_VIEW);

	virtual ~SubView()
	{
	}

	int getViewType() override
	{
		return CF_PANEL_VIEW;
	}

	void Refresh(Json::Value* context, std::map<int, SE2::ModuleView*>& guiObjectMap_) override {} // perhaps this should not derive from ViewBase, it does not have its own presenter, don't make sense.
	void BuildModules(Json::Value* context, std::map<int, SE2::ModuleView*>& guiObjectMap) override;

	std::string getSkinName() override
	{
		//// [Viewbase[<- parent -[ SubContainerView<- guihost -[ContainerPanel]
		//auto view = dynamic_cast<SE2::ViewChild*> (getGuiHost())->parent;
		//return view->getSkinName();

		return parent->parent->getSkinName();
	}

	void onValueChanged();
	bool isShown() override;

	int32_t setCapture(SE2::IViewChild* module) override;

	gmpi::ReturnCode initialize() override;
	gmpi::ReturnCode measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize) override;
	gmpi::ReturnCode arrange(const gmpi::drawing::Rect* finalRect) override;
	gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override;
	gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override;
	gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override;
	gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override;
	gmpi::ReturnCode onMouseWheel(gmpi::drawing::Point point, int32_t flags, int32_t delta) override;
// TODO	int32_t getToolTip(gmpi::drawing::Point point, gmpi::api::IString* returnString);
	void ChildInvalidateRect(const gmpi::drawing::Rect& invalidRect) override;

	void OnChildMoved() override;

	void calcBounds(gmpi::drawing::Rect & returnLayoutRect, gmpi::drawing::Rect & returnClipRect);

	gmpi::drawing::Point MapPointToView(ViewBase* parentView, gmpi::drawing::Point p) override
	{
		if(parentView == this)
			return p;

		// Forward chain from SubView child-local coords up toward the enclosing view:
		//   child-local              + offset_                   -> Container plugin-local
		//   Container plugin-local   + pluginGraphicsPos         -> Container module-local
		//   Container module-local   + bounds_.topleft           -> Container parent-view coord
		// Then recurse up until we reach parentView. This is the exact inverse of
		// ModuleView::OffsetToClient() (which subtracts both bounds_ and pluginGraphicsPos).
		auto moduleview = dynamic_cast<SE2::ModuleView*> (parent);

		p += offset_;

		if (moduleview)
		{
			p.x += moduleview->pluginGraphicsPos.left + moduleview->bounds_.left;
			p.y += moduleview->pluginGraphicsPos.top  + moduleview->bounds_.top;

			if (moduleview->parent)
				p = moduleview->parent->MapPointToView(parentView, p);
		}

		return p;
	}

	// Get the compound transform matrix from this SubView to the topmost view (including zoom and pan)
	gmpi::drawing::Matrix3x2 GetTransformToTopView() override
	{
		// Forward chain from a point in SubView child-local coords to doc coords,
		// then through the top-level view's pan/zoom:
		//   child-local              + offset_               -> Container plugin-local
		//   Container plugin-local   + pluginGraphicsPos     -> Container module-local
		//   Container module-local   + bounds_.topleft       -> doc
		//   doc                      * TopView.viewTransform -> window
		// This is the exact inverse of ModuleView::OffsetToClient(), which subtracts
		// both bounds_ and pluginGraphicsPos.
		auto transform = gmpi::drawing::makeTranslation(offset_.width, offset_.height);

		// Get parent ModuleView (the Container module that owns this SubView)
		auto moduleview = dynamic_cast<SE2::ModuleView*>(parent);
		if (moduleview)
		{
			// Container plugin-local -> Container module-local
			transform = transform * gmpi::drawing::makeTranslation(
				moduleview->pluginGraphicsPos.left,
				moduleview->pluginGraphicsPos.top
			);

			// Container module-local -> doc
			transform = transform * gmpi::drawing::makeTranslation(
				moduleview->bounds_.left,
				moduleview->bounds_.top
			);

			// doc -> window (top-level pan/zoom)
			if (moduleview->parent)
			{
				transform = transform * moduleview->parent->GetTransformToTopView();
			}
		}

		return transform;
	}

	int32_t StartCableDrag(SE2::IViewChild* fromModule, int fromPin, gmpi::drawing::Point dragStartPoint, gmpi::drawing::Point mousePoint) override;

	// ISubView
	void OnCableDrag(SE2::ConnectorViewBase* dragline, gmpi::drawing::Point dragPoint, float& bestDistance, SE2::ModuleView*& bestModule, int& bestPinIndex) override;
	bool hitTest(int32_t flags, gmpi::drawing::Point* point) override;
	bool MP_STDCALL isVisible() override
	{
		return isShown();
	}
	void process() override;

	void OnPatchCablesVisibilityUpdate() override;
	void markDirtyChild(SE2::IViewChild* child) override;
	SE2::ConnectorViewBase* createCable(SE2::CableType type, int32_t handleFrom, int32_t fromPin) override;

	gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
	{
		if (*iid == *(const gmpi::api::Guid*)&ISubView::guid)
		{
			*returnInterface = static_cast<ISubView*>(this);
			addRef();
			return gmpi::ReturnCode::Ok;
		}

		return SE2::ViewBase::queryInterface(iid, returnInterface);
	}

	GMPI_REFCOUNT
};
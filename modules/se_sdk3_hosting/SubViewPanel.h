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
	gmpi::editor::Pin<bool> showControlsOnModule;
	gmpi::editor::Pin<bool> showControls;

	gmpi::drawing::Rect viewBounds;
	gmpi::drawing::Rect viewClipBounds;
	int parentViewType = 0;
	SE2::ModuleView* parent = {};

	// SubView's pan is a simple translation stored in the inherited viewTransform /
	// inv_viewTransform. panInitialized_ tracks whether the pan has been computed
	// yet (replaces the old "-99999" sentinel on the removed offset_ member).
	bool panInitialized_ = false;

	// Translate-only helpers that keep viewTransform, viewTransformPrecise and
	// inv_viewTransform in sync. SubView has no zoom or rotation.
	void setPan(float x, float y)
	{
		viewTransform = gmpi::drawing::makeTranslation(x, y);
		viewTransformPrecise = viewTransform;
		inv_viewTransform = gmpi::drawing::makeTranslation(-x, -y);
		panInitialized_ = true;
	}
	float panX() const { return viewTransform._31; }
	float panY() const { return viewTransform._32; }

public:
	SubView() : SE2::ViewBase({ 1000, 1000 })
	{
		init(showControlsLegacy);
		init(showControlsOnModule);
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
	// ChildInvalidateRect not overridden — ViewBase::ChildInvalidateRect uses
	// viewTransform which SubView keeps synced via setPan().

	void OnChildMoved() override;

	void calcBounds(gmpi::drawing::Rect & returnLayoutRect, gmpi::drawing::Rect & returnClipRect);
	gmpi::drawing::Rect getClipArea()
	{
		return viewClipBounds;
	}

	gmpi::drawing::Point MapPointToView(ViewBase* parentView, gmpi::drawing::Point p) override
	{
		if(parentView == this)
			return p;

		// Forward chain from SubView child-local coords up toward the enclosing view:
		//   child-local              * viewTransform             -> Container plugin-local
		//   Container plugin-local   + pluginGraphicsPos         -> Container module-local
		//   Container module-local   + bounds_.topleft           -> Container parent-view coord
		// Then recurse up until we reach parentView.
		p = p * viewTransform;

		auto moduleview = dynamic_cast<SE2::ModuleView*> (parent);
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
		//   child-local              * viewTransform         -> Container plugin-local
		//   Container plugin-local   + pluginGraphicsPos     -> Container module-local
		//   Container module-local   + bounds_.topleft       -> doc
		//   doc                      * TopView.viewTransform -> window
		auto transform = viewTransform;

		// Get parent ModuleView (the Container module that owns this SubView)
		auto moduleview = parent;
		if (moduleview)
		{
			// Container plugin-local -> Container module-local -> doc
			transform = transform * gmpi::drawing::makeTranslation(
				moduleview->pluginGraphicsPos.left + moduleview->bounds_.left,
				moduleview->pluginGraphicsPos.top  + moduleview->bounds_.top
			);

			// doc -> window (top-level pan/zoom)
			if (moduleview->parent)
				transform = transform * moduleview->parent->GetTransformToTopView();
		}

		return transform;
	}

	int32_t StartCableDrag(SE2::IViewChild* fromModule, int fromPin, gmpi::drawing::Point dragStartPoint, gmpi::drawing::Point mousePoint) override;

	// ISubView
	void OnCableDrag(SE2::ConnectorViewBase* dragline, gmpi::drawing::Point dragPoint, float& bestDistance, SE2::ModuleView*& bestModule, int& bestPinIndex) override;
	//bool hitTest(int32_t flags, gmpi::drawing::Point* point) override;
	gmpi::ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override;

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
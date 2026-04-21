#pragma once
#include "./ModuleView.h"
#include "./ViewBase.h"
#include "TimerManager.h"
#include "modules/CadmiumRenderer/Cadmium/GUI_3_0.h"

namespace SE2
{
	class IPresenter;
	class IViewChild;
}

// sub-view shown on Panel.
class SubViewCadmium : public SE2::ViewBase, public gmpi::api::IParameterObserver, public se_sdk::TimerClient
{
	BoolGuiPin showControlsLegacy;
	BoolGuiPin showControls;

	gmpi::drawing::Rect viewBounds;
	int parentViewType = 0;
	SE2::ViewBase* parent = {};

	functionalUI functionalUI;
	std::vector<node*> renderNodes2;
	std::unordered_map<int32_t, observableState*> nodeParameters; // parameter-handle, 'state' node

	bool OnTimer() override;

public:
	gmpi::drawing::Size offset_; // offset of children relative to bounds (not parent).

	SubViewCadmium(int pparentViewType = CF_PANEL_VIEW);

	virtual ~SubViewCadmium();

	void Refresh(Json::Value* context, std::map<int, SE2::ModuleView*>& guiObjectMap_) override {} // perhaps this should not derive from ViewBase, it does not have its own presenter, don't make sense.

	std::string getSkinName() override
	{
		return parent->getSkinName();
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
	gmpi::ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override;
	int32_t getToolTip(gmpi::drawing::Point point, gmpi::api::IString* returnString);
//	int32_t MP_STDCALL getToolTip(float x, float y, gmpi::IMpUnknown* returnToolTipString) override;

	void ChildInvalidateRect(const gmpi::drawing::Rect& invalidRect) override
	{
		const auto adjusted = offsetRect(invalidRect, offset_);
		parent->invalidateRect(&adjusted);
	}

	void OnChildMoved() override;

	void calcBounds(gmpi::drawing::Rect & returnLayoutRect, gmpi::drawing::Rect & returnClipRect);

	void BuildView(Json::Value* context);

	int32_t ChildCreatePlatformTextEdit(const gmpi::drawing::Rect* rect, gmpi_gui::IMpPlatformText** returnTextEdit)
	{
		(void)rect;
		(void)returnTextEdit;
		return gmpi::MP_NOSUPPORT;
	}

	int32_t ChildCreatePlatformMenu(const gmpi::drawing::Rect* rect, gmpi_gui::IMpPlatformMenu** returnMenu)
	{
		(void)rect;
		(void)returnMenu;
		return gmpi::MP_NOSUPPORT;
	}

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

	int32_t StartCableDrag(SE2::IViewChild* fromModule, int fromPin, gmpi::drawing::Point dragStartPoint, gmpi::drawing::Point mousePoint) override;
	virtual void OnCableDrag(SE2::ConnectorViewBase* dragline, gmpi::drawing::Point dragPoint, float& bestDistance, SE2::IViewChild*& bestModule, int& bestPinIndex);
	void OnPatchCablesVisibilityUpdate() override;

	// IParameterObserver
	gmpi::ReturnCode setParameter(int32_t parameterHandle, gmpi::Field fieldId, int32_t voice, int32_t size, const uint8_t* data) override;

	gmpi::ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
	{
		*returnInterface = nullptr;

		if (*iid == gmpi::api::IParameterObserver::guid)
		{
			*returnInterface = static_cast<gmpi::api::IParameterObserver*>(this);
			addRef();
			return gmpi::ReturnCode::Ok;
		}

		return SE2::ViewBase::queryInterface(iid, returnInterface);
	}
	GMPI_REFCOUNT;
};

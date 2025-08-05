#pragma once
#include "./ModuleView.h"
#include "./ViewBase.h"

namespace SE2
{
	class IPresenter;
	class IViewChild;
	class ConnectorViewBase;
}

//class DECLSPEC_NOVTABLE ISubView : public gmpi::IMpUnknown
//{
//public:
//	virtual void OnCableDrag(SE2::ConnectorViewBase* dragline, GmpiDrawing::Point dragPoint, float& bestDistance, SE2::IViewChild*& bestModule, int& bestPinIndex) = 0;
//	virtual bool hitTest(int32_t flags, GmpiDrawing_API::MP1_POINT* point) = 0;
//	virtual bool MP_STDCALL isVisible() = 0;
//};
//
//// GUID for ISubView
//static const gmpi::MpGuid SE_IID_SUBVIEW =
//// {4F6B4050-F169-401C-AAEB-D6057ECDF58E}
//{ 0x4f6b4050, 0xf169, 0x401c, { 0xaa, 0xeb, 0xd6, 0x5, 0x7e, 0xcd, 0xf5, 0x8e } };

// sub-view shown on Panel.
class SubView : public SE2::ViewBase, public ISubView
{
	BoolGuiPin showControlsLegacy;
	BoolGuiPin showControls;

	GmpiDrawing::Rect viewBounds;
	int parentViewType = 0;

public:
	GmpiDrawing_API::MP1_SIZE offset_; // offset of children relative to bounds (not parent).

	SubView(int pparentViewType = CF_PANEL_VIEW);

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
		// [Viewbase[<- parent -[ SubContainerView<- guihost -[ContainerPanel]
		auto view = dynamic_cast<SE2::ViewChild*> (getGuiHost())->parent;
		return view->getSkinName();
	}

	void onValueChanged();
	bool isShown() override;

	int32_t setCapture(SE2::IViewChild* module) override;

	// SeGuiVstGuiBase interface.
	int32_t initialize() override;
	int32_t measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE* returnDesiredSize) override;
	int32_t arrange(GmpiDrawing_API::MP1_RECT finalRect) override;
	int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override;
	int32_t onPointerDown(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
	int32_t onPointerMove(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
	int32_t onPointerUp(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
	int32_t onMouseWheel(int32_t flags, int32_t delta, GmpiDrawing_API::MP1_POINT point) override;
	int32_t hitTest(GmpiDrawing_API::MP1_POINT point) override;
	int32_t getToolTip(GmpiDrawing_API::MP1_POINT point, gmpi::IString* returnString) override;
//	int32_t getToolTip(float x, float y, gmpi::IMpUnknown* returnToolTipString) override;

	void ChildInvalidateRect(const GmpiDrawing_API::MP1_RECT& invalidRect) override
	{
		GmpiDrawing::Rect adjusted(invalidRect);
		adjusted.Offset(offset_);
		getGuiHost()->invalidateRect(&adjusted);
	}

	void OnChildMoved() override;

	void calcBounds(GmpiDrawing::Rect & returnLayoutRect, GmpiDrawing::Rect & returnClipRect);

	int32_t ChildCreatePlatformTextEdit(const GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformText** returnTextEdit) override
	{
		GmpiDrawing::Rect adjusted(*rect);
		adjusted.Offset(offset_);
		return getGuiHost()->createPlatformTextEdit(&adjusted, returnTextEdit);
	}

	int32_t ChildCreatePlatformMenu(const GmpiDrawing_API::MP1_RECT* rect, gmpi_gui::IMpPlatformMenu** returnMenu) override
	{
		GmpiDrawing::Rect adjusted(*rect);
		adjusted.Offset(offset_);
		return getGuiHost()->createPlatformMenu(&adjusted, returnMenu);
	}

	GmpiDrawing::Point MapPointToView(ViewBase* parentView, GmpiDrawing::Point p) override
	{
		if(parentView == this)
			return p;

		// [Viewbase[<- parent -[ SubContainerView<- guihost -[ContainerPanel]
		auto submview = dynamic_cast<SE2::ModuleView*> (getGuiHost());

		// My offset.
		p += offset_;

		// Parent ModuleView offset.
		p += submview->OffsetToClient();

		auto view = submview->parent;
		p = view->MapPointToView(parentView, p);

		return p;
	}

	int32_t StartCableDrag(SE2::IViewChild* fromModule, int fromPin, GmpiDrawing::Point dragStartPoint, bool isHeldAlt, SE2::CableType type) override;

	// ISubView
	void OnCableDrag(SE2::ConnectorViewBase* dragline, GmpiDrawing::Point dragPoint, float& bestDistance, SE2::IViewChild*& bestModule, int& bestPinIndex) override;
	bool hitTest(int32_t flags, GmpiDrawing_API::MP1_POINT* point) override;
	bool MP_STDCALL isVisible() override
	{
		return isShown();
	}
	void process() override;

	void OnPatchCablesVisibilityUpdate() override;
	void markDirtyChild(SE2::IViewChild* child) override;
	SE2::ConnectorViewBase* createCable(SE2::CableType type, int32_t handleFrom, int32_t fromPin) override;

	int32_t queryInterface(const gmpi::MpGuid& iid, void** returnInterface) override
	{
		if (iid == SE_IID_SUBVIEW)
		{
			*returnInterface = static_cast<ISubView*>(this);
			addRef();
			return gmpi::MP_OK;
		}

		return SE2::ViewBase::queryInterface(iid, returnInterface);
	}

	GMPI_REFCOUNT
};
#ifndef SLIDERGUI_H_INCLUDED
#define SLIDERGUI_H_INCLUDED

#include <algorithm>
#include "../se_sdk3/mp_sdk_gui2.h"
#include "../shared/ImageMetadata.h"
#include "../sharedLegacyWidgets/BitmapWidget.h"
#include "../sharedLegacyWidgets/EditWidget.h"
#include "../sharedLegacyWidgets/TextWidget.h"

class SliderGui : public gmpi_gui::MpGuiGfxBase
{
	BitmapWidget bitmap;
	TextWidget headerWidget;
	EditWidget edit;
	Widget* captureWidget;
	bool backwardCompatibleVerticalArrange = {};

public:
	static std::wstring SliderFloatToString(float val, int p_decimal_places = -1);

	SliderGui();

	int32_t initialize() override;

	int32_t setHost(gmpi::IMpUnknown* host) override;
	int32_t measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE* returnDesiredSize) override;
	int32_t arrange(GmpiDrawing_API::MP1_RECT finalRect) override;
	int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override;

	int32_t onPointerDown(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
	int32_t onPointerMove(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
	int32_t onPointerUp(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;

	int32_t populateContextMenu(float x, float y, gmpi::IMpUnknown* contextMenuItemsSink) override;
	int32_t onContextMenu(int32_t selection) override;
	int32_t getToolTip(GmpiDrawing_API::MP1_POINT point, gmpi::IString* returnString) override
	{
		auto utf8String = (std::string)pinHint;
		returnString->setData(utf8String.data(), static_cast<int32_t>(utf8String.size()));

		return gmpi::MP_OK;
	}
	int32_t onMouseWheel(int32_t flags, int32_t delta, GmpiDrawing_API::MP1_POINT point) override;

private:
	void onSetValueIn();
	void onSetAppearance();
	void onSetTitle();
	void UpdateValuePinFromBitmap();
	void UpdateValuePinFromEdit();
	void UpdateEditText();

	FloatGuiPin pinValueIn;
	StringGuiPin pinItemList;
	StringGuiPin pinHint;
	StringGuiPin pinTitle;
	FloatGuiPin pinNormalised;

 	StringGuiPin pinNameIn;
 	StringGuiPin pinMenuItems;
 	IntGuiPin pinMenuSelection;
 	BoolGuiPin pinMouseDown;
 	FloatGuiPin pinRangeLo;
	FloatGuiPin pinRangeHi;
//	FloatGuiPin pinResetValue;
	BoolGuiPin pinShowReadout;
	IntGuiPin pinAppearance;
//	BoolGuiPin pinShowTitle;
};

#endif



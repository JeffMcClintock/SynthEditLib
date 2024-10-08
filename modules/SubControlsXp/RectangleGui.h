#ifndef RECTANGLEGUI_H_INCLUDED
#define RECTANGLEGUI_H_INCLUDED

#include "../se_sdk3/Drawing.h"
#include "mp_sdk_gui2.h"

class RectangleGui : public gmpi_gui::MpGuiGfxBase
{
public:
	RectangleGui();

	void onChangeRadius();
	void onChangeTopCol();
	void onChangeBotCol();

	// overrides.
	int32_t arrange(GmpiDrawing_API::MP1_RECT finalRect) override;
	int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext ) override;
/*
	bool drawTestNext;
	int32_t onPointerDown(int32_t flags, GmpiDrawing_API::MP1_POINT point) override
	{
		drawTestNext = true;
		GmpiDrawing::Rect r(0, 0, 20, 20);
		getGuiHost()->invalidateRect(&r);
		return gmpi::MP_UNHANDLED;
	}
*/

private:
	void onRedraw();

	FloatGuiPin pinCornerRadius;
 	BoolGuiPin pinTopLeft;
 	BoolGuiPin pinTopRight;
 	BoolGuiPin pinBottomLeft;
 	BoolGuiPin pinBottomRight;
 	StringGuiPin pinTopColor;
 	StringGuiPin pinBottomColor;

	GmpiDrawing::Color topColor;
	GmpiDrawing::Color bottomColor;

	GmpiDrawing::PathGeometry geometry;
};

#endif



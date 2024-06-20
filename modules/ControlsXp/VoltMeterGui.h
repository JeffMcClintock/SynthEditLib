#ifndef VOLTMETERGUI_H_INCLUDED
#define VOLTMETERGUI_H_INCLUDED

#include "Drawing.h"
#include "ClassicControlGuiBase.h"

class VoltMeterGui : public ClassicControlGuiBase
{
public:
	VoltMeterGui();
	void onSetTitle();

	int32_t arrange(GmpiDrawing_API::MP1_RECT finalRect) override;
	int32_t measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE* returnDesiredSize) override;

	int32_t getToolTip(GmpiDrawing_API::MP1_POINT point, gmpi::IString* returnString) override
	{
		return gmpi::MP_UNHANDLED;
	}

private:
 	void onSetpatchValue();
	FloatGuiPin pinpatchValue;
};

#endif



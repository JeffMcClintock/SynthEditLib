#pragma once

#include "Drawing.h"
#include "mp_sdk_gui2.h"
#include "se_mp_extensions.h"

class PatchPointsGui : public gmpi_gui::MpGuiGfxBase
{
	const float radius = 9;

public:
	// overrides.
	int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext ) override;
	int32_t measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE* returnDesiredSize) override
	{
		*returnDesiredSize = GmpiDrawing::Size(20,20);
		return gmpi::MP_OK;
	}

	// MP_OK = hit, MP_UNHANDLED/MP_FAIL = miss.
	// Default to MP_OK to allow user to select by clicking.
	// point will always be within bounding rect.
	int32_t hitTest(GmpiDrawing_API::MP1_POINT point) override
	{
		const GmpiDrawing::Point center(10, 10);
		return radius * radius >= (point.x - center.x) * (point.x - center.x) + (point.y - center.y) * (point.y - center.y) ? gmpi::MP_OK : gmpi::MP_FAIL;
	}
};

class PatchPointsGuiIn : public PatchPointsGui
{
};



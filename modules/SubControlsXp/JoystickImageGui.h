#ifndef JOYSTICKIMAGEGUI_H_INCLUDED
#define JOYSTICKIMAGEGUI_H_INCLUDED

#include "../sharedLegacyWidgets/ImageBase.h"

class JoystickImageGui : public ImageBase
{
	FloatGuiPin pinAnimationPosition;
	IntGuiPin pinFrameCount;
	IntGuiPin pinFrameCountLegacy;
	BoolGuiPin pinJumpToMouse;
 	FloatGuiPin pinPositionX;
 	FloatGuiPin pinPositionY;
	BoolGuiPin pinMouseDown2;

public:
	JoystickImageGui();

	float getAnimationPos() override{
		return pinAnimationPosition;
	}
	void setAnimationPos(float p) override;
	void onLoaded() override;
	std::wstring getHint() override
	{
		return pinHint;
	}
	// overrides.
	int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override;
	int32_t onPointerDown(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
	int32_t onPointerMove(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
	int32_t onPointerUp(int32_t flags, GmpiDrawing_API::MP1_POINT point) override
	{
		auto r =  ImageBase::onPointerUp(flags, point);
		pinMouseDown2 = false;
		return r;
	}

	// MP_OK = hit, MP_UNHANDLED/MP_FAIL = miss.
	// Default to MP_OK to allow user to select by clicking.
	// point will always be within bounding rect.
	int32_t hitTest(GmpiDrawing_API::MP1_POINT point) override;
};

#endif



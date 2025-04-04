#pragma once

#include <chrono>
#include "../se_sdk3/mp_sdk_gui2.h"
#include "Scope3.h"
#include "../se_sdk3/TimerManager.h"
#include "../shared/FontCache.h"

class Scope3Gui :
	public gmpi_gui::MpGuiGfxBase, public TimerClient, public FontCacheClient
{
	GmpiDrawing::Bitmap cachedBackground_;
	bool timerRuning = false;

#ifdef DRAW_LINES_ON_BITMAP
	GmpiDrawing::Bitmap foreground_;
#endif
public:
	Scope3Gui();

	// IMpGraphics overrides.
	int32_t measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE* returnDesiredSize) override;
	int32_t arrange(GmpiDrawing_API::MP1_RECT finalRect) override;
	int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override;
	int32_t initialize() override;

	void onValueChanged( int voiceId );
	void onVoicesActiveChanged( int voiceId );
	void onPolyModeChanged();
	void DrawTrace(GmpiDrawing::Factory& factory, GmpiDrawing::Graphics& g, float* capturedata, GmpiDrawing::Brush& pen, float mid_y, float scale, int width);

	// TimerClient overide.
	bool OnTimer() override;

	BlobArrayGuiPin pinSamplesA;
	BlobArrayGuiPin pinSamplesB;
	FloatArrayGuiPin pinGates;
	BoolGuiPin pinPolyMode;

private:
	GmpiDrawing::TextFormat_readonly dtextFormat;
	FontMetadata* typeface_ = {};
	int newestVoice_;
	std::chrono::steady_clock::time_point VoiceLastUpdated[MP_VOICE_COUNT];
};

#pragma once

#include <chrono>
#include "../se_sdk3/mp_sdk_gui2.h"
#include "../shared/FontCache.h"

class FreqAnalyserGui :
	public gmpi_gui::MpGuiGfxBase, public FontCacheClient
{
	GmpiDrawing::Bitmap cachedBackground_;
	float GraphXAxisYcoord;
	float currentBackgroundSampleRate = 0.f;
	GmpiDrawing::PathGeometry geometry;
	GmpiDrawing::PathGeometry lineGeometry;

	struct binData
	{
		int index;
		float fraction;
	};
	std::vector<binData> pixelToBin;

	std::vector<GmpiDrawing::Point> graphValues;
	std::vector<GmpiDrawing::Point> responseOptimized;
	float samplerate = 44100;
	int spectrumCount = 0;

public:
	FreqAnalyserGui();

	// IMpGraphics overrides.
	int32_t measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE* returnDesiredSize) override;
	int32_t arrange(GmpiDrawing_API::MP1_RECT finalRect) override;
	int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override;
	
	void onModeChanged();
	void onValueChanged();

	BlobGuiPin pinSpectrum;
	IntGuiPin pinMode;
	IntGuiPin pinDbHigh;
	IntGuiPin pinDbLow;
};

// prevent MS CPP - 'swprintf' was declared deprecated warning
#if defined(_MSC_VER)
  #define _CRT_SECURE_NO_DEPRECATE
  #pragma warning(disable : 4996)
#endif

#include <stdio.h>  // for GCC.
#include <algorithm>
#include "../se_sdk3/mp_sdk_gui2.h"
#include "../shared/GraphHelpers.h"
#include "../shared/FontCache.h"
#include "./SpectrumAnalyserBase.h"
#include "../se_sdk3/TimerManager.h"

using namespace se_sdk;
using namespace gmpi;
using namespace GmpiDrawing;

class FreqAnalyser3Gui :
	public gmpi_gui::MpGuiGfxBase, public FontCacheClient, public SpectrumAnalyserBase, public TimerClient
{
	const float leftBorder = 22.0f;
	float rightBorder = 0;

	GmpiDrawing::Bitmap cachedBackground_;
	float GraphXAxisYcoord;
	float currentBackgroundSampleRate = 0.f;
	GmpiDrawing::PathGeometry geometry;
	GmpiDrawing::PathGeometry lineGeometry;

//	std::vector<GmpiDrawing::Point> graphValues;
//	std::vector<GmpiDrawing::Point> responseOptimized;
//	float samplerate = 44100;
//	int spectrumCount = 0;

public:
	FreqAnalyser3Gui();

	// IMpGraphics overrides.
	int32_t MP_STDCALL measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE* returnDesiredSize) override;
	int32_t MP_STDCALL arrange(GmpiDrawing_API::MP1_RECT finalRect) override;
	int32_t MP_STDCALL OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext) override;

	void onModeChanged();
	void onValueChanged();

	BlobGuiPin pinSpectrum;
	IntGuiPin pinMode;
	IntGuiPin pinDbHigh;
	IntGuiPin pinDbLow;

	void updatePaths(const std::vector<GmpiDrawing::Point>& graphValuesOptimized, const std::vector<GmpiDrawing::Point>& peakHoldValuesOptimized) override
	{
		geometry = nullptr;
		invalidateRect();
	}

	void InixPixelToBin(std::vector<SpectrumAnalyserBase::binData>& pixelToBin, int graphWidth, int numValues, float sampleRate) override
	{
		pixelToBin.clear();

		const float hz2bin = numValues / (0.5f * sampleRate);

		const float totalWidth = graphWidth;
		const float lowHz = 20.0f;
		const int interpolationExtraValues = 3;
		const int numBars = interpolationExtraValues + static_cast<int>(std::ceil(totalWidth / pixelToBinDx));
		const int prequelDummyValues = 1; // db bins have a dummy value at index 0, to reduce the number of checks when interpolating.

		int section = 0;
		int x = 0;
		for (int j = 0; j < numBars; ++j)
		{
			const float octave = x * 10.0f / totalWidth;
			const float hz = powf(2.0f, octave) * lowHz;

			const float bin = prequelDummyValues + hz * hz2bin;
			const float safeBin = std::clamp(bin, 0.f, (float)numValues - 1.0f);

			const int index = static_cast<int>(safeBin);
			const float fraction = safeBin - (float)index;
			pixelToBin.push_back(
				{
					index
					,fraction
	#ifdef _DEBUG
					,hz
	#endif
				}
			);

			x += pixelToBinDx;
		}
	}

	// TimerClient overide.
	bool OnTimer() override
	{
		constexpr float dbDecay = 4.f;
		decayGraph(dbDecay);

		invalidateRect();

		return true;
	}
};

FreqAnalyser3Gui::FreqAnalyser3Gui()
{
	initializePin(pinSpectrum, static_cast<MpGuiBaseMemberPtr2>(&FreqAnalyser3Gui::onValueChanged));
//	initializePin(pinMode, static_cast<MpGuiBaseMemberPtr2>(&FreqAnalyser3Gui::onModeChanged));
	initializePin(pinDbHigh, static_cast<MpGuiBaseMemberPtr2>(&FreqAnalyser3Gui::onModeChanged));
	initializePin(pinDbLow, static_cast<MpGuiBaseMemberPtr2>(&FreqAnalyser3Gui::onModeChanged));
}

void FreqAnalyser3Gui::onValueChanged()
{
	graphValues.clear();
	geometry = nullptr;
	invalidateRect();
}

void FreqAnalyser3Gui::onModeChanged()
{
	cachedBackground_ = nullptr;
	onValueChanged();

	StartTimer(50);
}

#define USE_CACHED_BACKGROUND 1

float calcTopFrequency3(float samplerate)
{
	return floorf(samplerate / 20000.0f) * 10000.0f;
}

int32_t FreqAnalyser3Gui::OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext)
{
	auto r = getRect();

	GmpiDrawing::Graphics g(drawingContext);
	ClipDrawingToBounds cd(g, r);

	constexpr float displayOctaves = 10;
	float displayDbRange = (float)(std::max)(10, pinDbHigh - pinDbLow);
	float displayDbMax = (float) pinDbHigh;

	const float snapToPixelOffset = 0.5f;

	float width = r.right - r.left;
	float height = r.bottom - r.top;

	float scale = height * 0.46f;
	float mid_x = floorf(0.5f + width * 0.5f);
	float mid_y = floorf(0.5f + height * 0.5f);

	float bottomBorder = leftBorder;
	float graphWidth = rightBorder - leftBorder;

	const auto newSpectrumCount = (std::max)(0, -1 + static_cast<int>(pinSpectrum.rawSize() / sizeof(float)));
	auto capturedata = (const float*) pinSpectrum.rawData();

	if (newSpectrumCount > 0)
	{
		const auto& newSamplerate = capturedata[newSpectrumCount]; // last entry used for sample-rate.

		// Invalidate background if SRT changes.
		if( currentBackgroundSampleRate != sampleRateFft)
			cachedBackground_ = nullptr;

		if (sampleRateFft != newSamplerate || rawSpectrum.size() != newSpectrumCount)
		{
			sampleRateFft = newSamplerate;
			pixelToBin.clear();
			graphValues.clear();
		}
	}

#if USE_CACHED_BACKGROUND
	if (cachedBackground_.isNull())
	{
		auto dc = g.CreateCompatibleRenderTarget(Size(r.getWidth(), r.getHeight()));
		dc.BeginDraw();
#else
	{
		auto& dc = g;
#endif
		currentBackgroundSampleRate = sampleRateFft;

		auto dtextFormat = g.GetFactory().CreateTextFormat(10); // GetTextFormat(getHost(), getGuiHost(), "tty", &typeface_);

		dtextFormat.SetTextAlignment(TextAlignment::Leading); // Left
		dtextFormat.SetParagraphAlignment(ParagraphAlignment::Center);
		dtextFormat.SetWordWrapping(WordWrapping::NoWrap); // prevent word wrapping into two lines that don't fit box.

		auto gradientBrush = g.CreateLinearGradientBrush(Color::FromRgb(0x39323A), Color::FromRgb(0x080309), Point(0, 0), Point(0, height) );
		dc.FillRectangle(r, gradientBrush);

		auto fontBrush = g.CreateSolidColorBrush(Color::Gold);
		auto brush2 = g.CreateSolidColorBrush(Color::Gray);
		float penWidth = 1.0f;
		auto fontHeight = dtextFormat.GetTextExtentU("M").height;

		// dB labels.
		float lastTextY = -10;
		if (height > 30)
		{
			float db = displayDbMax;
			float y = 0;
			while (true)
			{
				y = (displayDbMax - db) * (height - bottomBorder) / displayDbRange;
				y = snapToPixelOffset + floorf(0.5f + y);

				if (y >= height - fontHeight)
				{
					break;
				}

				GraphXAxisYcoord = y;

				dc.DrawLine(GmpiDrawing::Point(leftBorder, y), GmpiDrawing::Point(rightBorder, y), brush2, penWidth);

				if (y > lastTextY + fontHeight * 1.2)
				{
					lastTextY = y;
					char txt[10];
					sprintf(txt, "%3.0f", (float)db);

					//				TextOut(hDC, 0, y - fontHeight / 2, txt, (int)wcslen(txt));
					GmpiDrawing::Rect textRect(0, y - fontHeight / 2, 30, y + fontHeight / 2);
					dc.DrawTextU(txt, dtextFormat, textRect, fontBrush);
				}

				db -= 10.0f;
			}

			// extra line at -3dB. To help check filter cuttoffs.
			db = -3.f;
			y = (displayDbMax - db) * (height - bottomBorder) / displayDbRange;
			y = snapToPixelOffset + floorf(0.5f + y);

			dc.DrawLine(GmpiDrawing::Point(leftBorder, y), GmpiDrawing::Point(rightBorder, y), brush2, penWidth);
		}

//		if (pinMode == 0) // Log
		{
			// FREQUENCY LABELS
			// Highest mark is nyquist rounded to nearest 10kHz.
			const float topFrequency = calcTopFrequency3(sampleRateFft);
			float frequencyStep = 1000.0;
			if (width < 500)
			{
				frequencyStep = 10000.0;
			}
			float hz = topFrequency;
			float previousTextLeft = INT_MAX;
			float x = INT_MAX;
			do {
				const float octave = displayOctaves - logf(topFrequency / hz) / logf(2.0f);

				x = leftBorder + graphWidth * octave / displayOctaves;

				// hmm, can be misleading when grid line is one pixel off due to snapping
//				x = snapToPixelOffset + floorf(0.5f + x);

				if (x <= leftBorder || hz < 5.0)
					break;

				bool extendLine = false;

				// Text.
				if (sampleRateFft > 0.0f)
				{
					char txt[10];

					// Large values printed in kHz.
					if (hz > 999.0)
					{
						sprintf(txt, "%.0fk", hz * 0.001);
					}
					else
					{
						sprintf(txt, "%.0f", hz);
					}

					//				int stringLength = strlen(txt);
					//SIZE size;
					//::GetTextExtentPoint32(hDC, txt, stringLength, &size);
					auto size = dtextFormat.GetTextExtentU(txt);
					// Ensure text don't overwrite text to it's right.
					if (x + size.width / 2 < previousTextLeft)
					{
						extendLine = true;
						//					TextOut(hDC, x, height - fontHeight, txt, stringLength);

						GmpiDrawing::Rect textRect(x - size.width / 2, height - fontHeight, x + size.width / 2, height);
						dc.DrawTextU(txt, dtextFormat, textRect, fontBrush);

						previousTextLeft = x - (2 * size.width) / 3; // allow for text plus whitepace.
					}
				}

				// Vertical line.
				float lineBottom = height - fontHeight;
				if (!extendLine)
				{
					lineBottom = GraphXAxisYcoord;
				}
				dc.DrawLine(GmpiDrawing::Point(x, 0), GmpiDrawing::Point(x, lineBottom), brush2, penWidth);

				//if (hz > 950 && hz < 1050)
				//{
				//	_RPTN(0, "1k line @ %f\n", x);
				//}

				if (frequencyStep > hz * 0.99f)
				{
					frequencyStep *= 0.1f;
				}

				hz = hz - frequencyStep;

			} while ( true);
		}
#if 0
		else
		{
			// FREQUENCY LABELS
			// Highest mark is nyquist rounded to nearest 2kHz.
			float topFrequency = floor(samplerate / 2000.0f) * 1000.0f;
			float frequencyStep = 1000.0;
			float hz = topFrequency;
			float previousTextLeft = INT_MAX;
			float x = INT_MAX;
			do {
				x = leftBorder + (2.0f * hz * graphWidth) / samplerate;
				x = snapToPixelOffset + floorf(0.5f + x);

				if (x <= leftBorder || hz < 5.0)
					break;

				bool extendLine = false;

				// Text.
				if (samplerate > 0)
				{
					char txt[10];

					// Large values printed in kHz.
					if (hz > 999.0)
					{
						sprintf(txt, "%.0fk", hz * 0.001);
					}
					else
					{
						sprintf(txt, "%.0f", hz);
					}

					auto size = dtextFormat.GetTextExtentU(txt);
					// Ensure text don't overwrite text to it's right.
					if (x + size.width / 2 < previousTextLeft)
					{
						extendLine = true;
						//					TextOut(hDC, x, height - fontHeight, txt, stringLength);

						GmpiDrawing::Rect textRect(x - size.width / 2, height - fontHeight, x + size.width / 2, height);
						dc.DrawTextU(txt, dtextFormat, textRect, fontBrush);

						previousTextLeft = x - (2 * size.width) / 3; // allow for text plus whitepace.
					}
				}

				// Vertical line.
				float lineBottom = height - fontHeight;
				if (!extendLine)
				{
					lineBottom = GraphXAxisYcoord;
				}
				dc.DrawLine(GmpiDrawing::Point(x, 0), GmpiDrawing::Point(x, lineBottom), brush2, penWidth);

				hz = hz - frequencyStep;

			} while (true);
		}
#endif
#if USE_CACHED_BACKGROUND
		dc.EndDraw();
		cachedBackground_ = dc.GetBitmap();
#endif
	}

#if USE_CACHED_BACKGROUND
	g.DrawBitmap(cachedBackground_, Point(0, 0), r);
#endif

	if (graphValues.empty())
	{
		rawSpectrum.assign(capturedata, capturedata + newSpectrumCount);
		const auto plotAreaWidth = rightBorder - leftBorder;
		updateSpectrumGraph(plotAreaWidth, r.getHeight());
	}

	if(geometry.isNull() && !graphValuesOptimized.empty())
	{
        auto factory = g.GetFactory();
   
		geometry = factory.CreatePathGeometry();
		auto sink = geometry.Open();

		lineGeometry = factory.CreatePathGeometry();
		auto lineSink = lineGeometry.Open();

		sink.BeginFigure(leftBorder, GraphXAxisYcoord, FigureBegin::Filled);
		float inverseN = 2.0f / rawSpectrum.size();
		const float dbc = 20.0f * log10f(inverseN);
		// Log
		{
			graphValues.clear();

            graphValues.push_back({leftBorder, 0.0f}); // we will update y once we have it
            
            assert(graphValuesOptimized.size() > 1);
                       
            bool first = true;
			for (auto& p : graphValuesOptimized)
			{
				Point point(p.x + leftBorder, p.y);

                if(first)
                {
                    lineSink.BeginFigure(point);
                    first = false;
                }
                else
                {
       				lineSink.AddLine(point);
                }
                
				sink.AddLine(point);
			}

            // complete filled area down to axis
			sink.AddLine(GmpiDrawing::Point(graphValuesOptimized.back().x + leftBorder, GraphXAxisYcoord));
		}

		lineSink.EndFigure(FigureEnd::Open);
		lineSink.Close();
		sink.EndFigure();
		sink.Close();
	}

	if (geometry && lineGeometry)
	{
		auto graphColor = Color::FromArgb(0xFF65B1D1);
		auto brush2 = g.CreateSolidColorBrush(graphColor);
		const float penWidth = 1;
		Color fill = Color::FromArgb(0xc08BA7BF);

		auto plotClip = r;
		plotClip.left = leftBorder;
		plotClip.bottom = GraphXAxisYcoord;
		ClipDrawingToBounds cd2(g, plotClip);

		g.FillGeometry(geometry, g.CreateSolidColorBrush(fill));
		g.DrawGeometry(lineGeometry, brush2, penWidth);
	}

	return gmpi::MP_OK;
}

int32_t FreqAnalyser3Gui::measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE* returnDesiredSize)
{
	const float minSize = 15;

	returnDesiredSize->width = (std::max)(minSize, availableSize.width);
	returnDesiredSize->height = (std::max)(minSize, availableSize.height);

	return gmpi::MP_OK;
}

int32_t FreqAnalyser3Gui::arrange(GmpiDrawing_API::MP1_RECT finalRect)
{
	cachedBackground_.setNull();
	pixelToBin.clear();

#ifdef DRAW_LINES_ON_BITMAP
	foreground_.setNull();
#endif
	
	rightBorder = (finalRect.right - finalRect.left) - leftBorder * 0.5f;

	return MpGuiGfxBase::arrange(finalRect);
}

GMPI_REGISTER_GUI(MP_SUB_TYPE_GUI2, FreqAnalyser3Gui, L"SE Freq Analyser3");
SE_DECLARE_INIT_STATIC_FILE(FreqAnalyser3_Gui);

#include "./GraphicsFrameRateGui.h"
#include <algorithm>
#include "../se_sdk3/Drawing.h"

using namespace gmpi;
using namespace GmpiDrawing;

GMPI_REGISTER_GUI(MP_SUB_TYPE_GUI2, GraphicsFrameRateGui, L"SE Graphics Frame Rate" );

GraphicsFrameRateGui::GraphicsFrameRateGui() :
	framesPerSecond(0)
{
	text[0] = 0;
}

int32_t GraphicsFrameRateGui::initialize()
{
	auto r = gmpi_gui::MpGuiGfxBase::initialize(); // ensure all pins initialised (so widgets are built).

	StartTimer(4);

	return r;
}

int32_t GraphicsFrameRateGui::OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext )
{
	// The host renders once per overlapping dirty rect, so count only the first render after each tick.
	if (tickPending)
	{
		tickPending = false;
		++frameCount;
	}

	snprintf(text, std::size(text), "%4.1f FPS", framesPerSecond);

	Graphics g(drawingContext);

	if (textFormat.isNull())
	{
		textFormat = GetGraphicsFactory().CreateTextFormat();
		brush = g.CreateSolidColorBrush(Color::Red);
	}

    GmpiDrawing::Rect rect(0, 0, 100, 30);
	g.DrawTextU(text, textFormat, rect, brush);

// problematic
//	invalidateRect();

	return gmpi::MP_OK;
}

bool GraphicsFrameRateGui::OnTimer()
{
	const auto now = std::chrono::steady_clock::now();
	const auto elapsed = std::chrono::duration<float>(now - measureStart).count();
	if (elapsed >= 1.0f)
	{
		framesPerSecond = frameCount / elapsed;
		frameCount = 0;
		measureStart = now;
	}

	tickPending = true;
	invalidateRect();
	return true;
}

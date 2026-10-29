#include "mp_sdk_gui2.h"
#include "Drawing.h"
#include "TimerManager.h"
#include "CpuConsumption.h"

SE_DECLARE_INIT_STATIC_FILE(CpuMeterGui);

using namespace gmpi;
using namespace GmpiDrawing;

class CpuMeterGui : public gmpi_gui::MpGuiGfxBase, public se_sdk::TimerClient
{
	float peakCpu = 0.0f;

public:
	CpuMeterGui()
	{
		StartTimer(20);
	}

	bool OnTimer() override
	{
		invalidateRect();
		return true; // continue timer.
	}

	int32_t OnRender(GmpiDrawing_API::IMpDeviceContext* drawingContext ) override
	{
		Graphics g(drawingContext);

		auto r = getRect();
		ClipDrawingToBounds cd(g, r);

		auto textFormat = GetGraphicsFactory().CreateTextFormat();
		auto brush = g.CreateSolidColorBrush(Color::Green);

		const auto cpu = editorCpuLoad ? editorCpuLoad() : std::tuple<float, float>{}; // no editor in a plugin
		auto median = std::get<0>(cpu);
		auto peak = std::get<1>(cpu);
		peakCpu = (std::min)(peakCpu, 2.0f); // limit time required to fall from startup peak.
		peakCpu -= 0.001f * static_cast<float>(CPU_BATCH_SIZE);
		peakCpu = (std::max)(peakCpu - 0.01f, peak);

		if (peakCpu > 1.0f)
		{
			peakCpu = 1.0f;
			brush.SetColor(Color::Red);
		}
		else
		{
			brush.SetColor(Color::Green);
		}

		g.FillRectangle(Rect(0, (1.0f - peakCpu) * r.getHeight(), r.getWidth(), r.getHeight()), brush);

		brush.SetColor(Color::Lime);
		g.FillRectangle(Rect(0, (1.0f - median) * r.getHeight(), r.getWidth(), r.getHeight()), brush);

		brush.SetColor(Color::Black);
		char text[20];
		float cpuPercent = std::get<0>(cpu) * 100.0f;
		if(cpuPercent < 10.0f)
			snprintf(text, std::size(text), "%0.1f", cpuPercent);
		else
			snprintf(text, std::size(text), "%0.0f", cpuPercent);

		g.DrawTextU(text, textFormat, 10.0f, r.getHeight() - 20.0f, brush);

		return gmpi::MP_OK;
	}
};

namespace
{
	auto r = sesdk::Register<CpuMeterGui>::withXml(
R"XML(
<?xml version="1.0" ?>
<PluginList>
  <Plugin id="SE CPU Meter" name="CPU Meter" category="Debug" >
    <GUI graphicsApi="GmpiGui"/>
  </Plugin>
</PluginList>
)XML");

}

#include <optional>
#include <algorithm>
#include <format>
#include <filesystem>
#include <charconv>
#include "helpers/GmpiPluginEditor.h"
#include "helpers/GmpiPluginEditor2.h"
#include "helpers/CachedBlur.h"
#include "helpers/AnimatedBitmap.h"
//#include "NumberEditClient.h"
//#include "Extensions/EmbeddedFile.h"

using namespace gmpi;
using namespace gmpi::editor2;
using namespace gmpi::drawing;

SE_DECLARE_INIT_STATIC_FILE(HoverScopeGui)

class HoverScopeGui final : public PluginEditor
{
    std::chrono::steady_clock::time_point VoiceLastUpdated[128];

protected:
    Pin<Blob> pinCaptureData;
    PolyPin<float> pinGate;
    Pin<bool> pinPolyMode;

public:
    HoverScopeGui()
    {
        init(pinCaptureData);
        init(pinGate);
        init(pinPolyMode);

        pinCaptureData.onUpdate = [this](PinBase*) { reDraw(); };
//		pinAnimationPosition.onUpdate = [this](PinBase*) { calcDrawAt(); };
//		pinFrame.onUpdate = [this](PinBase*) { calcDrawAt(); };

		std::fill(std::begin(VoiceLastUpdated), std::end(VoiceLastUpdated), std::chrono::steady_clock::now());
    }

    void reDraw()
    {
		drawingHost->invalidateRect(nullptr);
    }

    void onLoaded()
    {
        //if (image.metadata())
        //{
        //    int fc = image.metadata()->getFrameCount();
        //    pinFrameCount = fc;
        //}
    }

    ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
    {
        Graphics g(drawingContext);

		ClipDrawingToBounds clip(g, bounds);

		g.clear(gmpi::drawing::Colors::Orange);

        auto geometry = g.getFactory().createPathGeometry();
        auto sink = geometry.open();

		sink.beginFigure({ 0, 0 }, FigureBegin::Hollow);

		const auto numPoints = pinCaptureData.value.size() / sizeof(float);

		float* amplitude = reinterpret_cast<float*>(pinCaptureData.value.data());
		for (int i = 0; i < numPoints; i += 2)
		{
			auto x = static_cast<float>(i) / numPoints * getWidth(bounds);
			auto y = (1.0f + *amplitude) * getHeight(bounds) * 0.5f;
			sink.addLine({ x, y });

			amplitude++;
		}

        sink.endFigure(FigureEnd::Open);
        sink.close();

		auto brush = g.createSolidColorBrush(gmpi::drawing::Colors::Black);
        g.drawGeometry(geometry, brush, 1.f);

        return ReturnCode::Ok;
    }

    ReturnCode measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize) override
    {
		*returnDesiredSize = { 64, 16 };
        return ReturnCode::Ok;
    }

    ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override
    {
        return ReturnCode::Unhandled;
    }
};

// Register the GUI
namespace
{
auto r = gmpi::Register<HoverScopeGui>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: HoverScope" name="HoverScope" category="Debug">
    <Parameters>
      <Parameter name="Capture Data A" datatype="blob" private="true" ignorePatchChange="true" isPolyphonic="true" persistant="false"/>
      <Parameter name="polyDetect" datatype="bool" private="true" ignorePatchChange="true" persistant="false"/>
    </Parameters>
	<GUI graphicsApi="GmpiGui">
      <Pin name="Capture Data A" datatype="blob" parameterId="0" private="true" isPolyphonic="true"/>
      <Pin name="VoiceGate" datatype="float" hostConnect="Voice/Gate" isPolyphonic="true" private="true"/>
      <Pin name="polydetect" datatype="bool" parameterId="1" />
	</GUI>
    <Audio>
      <Pin name="Capture Data A" direction="out" datatype="blob" parameterId="0" private="true" isPolyphonic="true"/>
      <Pin name="Signal A" datatype="float" rate="audio"/>
      <Pin name="VoiceActive" hostConnect="Voice/Active" datatype="float" isPolyphonic="true" />
      <Pin name="polydetect" direction="out" datatype="bool" parameterId="1"/>
    </Audio>
  </Plugin>
</PluginList>
)XML");
}

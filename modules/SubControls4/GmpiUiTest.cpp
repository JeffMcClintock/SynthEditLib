#include <optional>
#include <algorithm>
#include <format>
#include "helpers/GmpiPluginEditor.h"
#include "helpers/CachedBlur.h"
#include "NumberEditClient.h"

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::drawing;

class GmpiUiTest : public PluginEditor, public SsgNumberEditClient
{
    cachedBlur blur;
    SsgNumberEdit numberEdit;
    float value{23.5f};

public:

	GmpiUiTest() : numberEdit(*this)
	{
    }

    ReturnCode open(gmpi::api::IUnknown* host) override
    {
        const auto s = std::format("{:.2f}", value);

        numberEdit.setText(s);
        return PluginEditor::open(host);
    }

	ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);

        ClipDrawingToBounds _(g, bounds);
        g.clear(Colors::Black);

        // draw the object blurred
		blur.draw(
              g
            , bounds
            , [](Graphics& g)
			{
				auto brush = g.createSolidColorBrush(Colors::White); // always draw the mask in white. Change the final color via blur.tint
				g.drawCircle({ 50, 50 }, 40, brush, 5.0f);
			}
        );

        // draw the same object sharp, over the top of the blur.
        auto brush = g.createSolidColorBrush(Colors::White);
        g.drawCircle({ 50, 50 }, 40, brush, 5.0f);

        // draw the value as text
        auto textFormat = g.getFactory().createTextFormat(22);
        numberEdit.render(g, textFormat, bounds);

		return ReturnCode::Ok;
	}

    // IInputClient
    gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override
    {
        return inputHost->setCapture();
    }
    gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override
    {
        return ReturnCode::Unhandled;
    }
    gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override
    {
		inputHost->getFocus();

        numberEdit.show(dialogHost, &bounds);

        return inputHost->releaseCapture();
    }
    gmpi::ReturnCode OnKeyPress(wchar_t c) override
    {
        return ReturnCode::Handled;
    }

    // SsgNumberEditClient
    void repaintText() override
    {
        drawingHost->invalidateRect({});
    }
    void setEditValue(std::string value) override {}
    void endEditValue() override {}
};

// Describe the plugin and register it with the framework.
namespace
{
auto r = gmpi::Register<GmpiUiTest>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: GmpiUiTest" name="GMPI-UI Test" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI graphicsApi="GmpiGui"/>
  </Plugin>
</PluginList>
)XML");
}
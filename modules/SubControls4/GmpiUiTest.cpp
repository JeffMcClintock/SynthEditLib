#include <optional>
#include <algorithm>
#include <format>
#include <charconv>
#include "helpers/GmpiPluginEditor.h"
#include "helpers/CachedBlur.h"
#include "NumberEditClient.h"

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::drawing;

SE_DECLARE_INIT_STATIC_FILE(GmpiUiTest)

class GmpiUiTest : public PluginEditor, public SsgNumberEditClient
{
    cachedBlur blur;
    SsgNumberEdit numberEdit;
    float value{23.5f};
	bool isHovered{};

	void updateTextFromValue()
	{
		const auto s = std::format("{:.2f}", value);
		numberEdit.setText(s);
	}

public:
	GmpiUiTest() : numberEdit(*this)
	{
    }

    ReturnCode open(gmpi::api::IUnknown* host) override
    {
		updateTextFromValue();
        return PluginEditor::open(host);
    }

    ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
    {
        Graphics g(drawingContext);

        ClipDrawingToBounds _(g, bounds);
        g.clear(Colors::Black);

        if (isHovered)
        {
            // draw the object blurred
            blur.draw(
                g
                , bounds
                , [this](Graphics& g)
                {
                    auto brush = g.createSolidColorBrush(Colors::White); // always draw the mask in white. Change the final color via blur.tint
                    g.drawCircle({ 50, 50 }, 40, brush, 5.0f);

                    auto textFormat = g.getFactory().createTextFormat(22);
                    numberEdit.render(g, textFormat, bounds);
                }
            );

        }

        auto brush = g.createSolidColorBrush(Colors::White);

        // draw the same object sharp, over the top of the blur.
        g.drawCircle({ 50, 50 }, 40, brush, 5.0f);

        // draw the value as text
        auto textFormat = g.getFactory().createTextFormat(22);
        numberEdit.render(g, textFormat, bounds);

        // draw some perfectly snapped pixels.
        {
            // this will transform logical to physical pixels.
            auto toPixels = makeScale(drawingHost->getRasterizationScale());
            auto dipToPixel = g.getTransform() * toPixels;

            // calc my top-left in pixels, snapped to exact pixel boundary.
            Point topLeftDip{ 0, 0 };
            auto pixelTopLeft = transformPoint(dipToPixel, topLeftDip);
            pixelTopLeft.x = std::round(pixelTopLeft.x);
            pixelTopLeft.y = std::round(pixelTopLeft.y);

            // this will transform physical pixels to logical pixels. Relative to swapchain top left.
            auto pixelToDip = invert(dipToPixel);

            auto brush = g.createSolidColorBrush(Colors::White); // always draw the mask in white. Change the final color via blur.tint

            Rect r;
            for (float y = 0; y < 60; ++y)
            {
                r.top = pixelTopLeft.y + y;
                r.bottom = r.top + 1.f;
                for (float x = 0; x < 60; ++x)
                {
                    r.left = pixelTopLeft.x + x;
                    r.right = r.left + 1.f;

                    if ((int)(x + y) % 2 == 0)
                    {
                        auto pixelRect = transformRect(pixelToDip, r);
                        g.fillRectangle(pixelRect, brush);
                    }
                }
            }
        }

		return ReturnCode::Ok;
	}

    // IInputClient
    gmpi::ReturnCode setHover(bool isMouseOverMe) override
    {
		isHovered = isMouseOverMe;
		drawingHost->invalidateRect({});
        return ReturnCode::Ok;
    }
    gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override
    {
		if (0 == (flags & gmpi::api::GG_POINTER_FLAG_FIRSTBUTTON)) // left button
            return ReturnCode::Unhandled;

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
		blur.invalidate();
        drawingHost->invalidateRect({});
    }
    void setEditValue(std::string s) override
    {
		// value = std::stof(svalue); // throws.
        value = 0.0f;
// not on apple yet        std::from_chars<float>(s.begin(), s.end(), value);
        value = atof(s.c_str());
        
        updateTextFromValue();
    }
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

// TODO create an interface to make these truely functional
// no state, just a function to transform input values to output values.
class ReallyFunctional
{
    struct myInputs
    {
        float* value1;
		float* value2;
    };
    struct myoutput
    {
		float* outputvalue1;
    };

	void process(void* inputs, void* outputs)
	{
		auto& in = *static_cast<myInputs*>(inputs);
		auto& out = *static_cast<myoutput*>(outputs);

		*out.outputvalue1 = *in.value1 + *in.value2;
	}
};

class PatchMemSet final : public PluginEditorNoGui
{
    Pin<int32_t> pinId;
    Pin<float> pinNormalized;
    Pin<bool> pinMouseDown;

	gmpi::shared_ptr <gmpi::api::IParameterSetter_x> paramHost;

public:
    PatchMemSet()
    {
        init(pinId);
        init(pinNormalized);
        init(pinMouseDown);

        pinNormalized.onUpdate = [this](PinBase*)
            {
                recalc();
            };
    }
    ReturnCode initialize() override
    {
        paramHost = editorHost.as<gmpi::api::IParameterSetter_x>();
        recalc();
		return PluginEditorNoGui::initialize();
    }

    void recalc()
    {
		if (paramHost)
		{
            paramHost->setParameter(pinId.value, gmpi::Field::Normalized, 0, sizeof(float), &pinNormalized.value);
		}
    }
};

namespace
{
auto r2 = gmpi::Register<PatchMemSet>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: PatchMemSet" name="Value Set" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="ID" datatype="int"/>
      <Pin name="Normalized" datatype="float"/>
      <Pin name="MouseDown" datatype="bool"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

class PatchMemGet final : public PluginEditorNoGui
{
//    Pin<int32_t> pinId_in;
    Pin<float> pinNormalized_in;
    Pin<bool> pinMouseDown_in;

    Pin<int32_t> pinId;
    Pin<float> pinNormalized;
    Pin<bool> pinMouseDown;

public:
    PatchMemGet()
    {
//        init(pinId_in);
        init(pinNormalized_in);
        init(pinMouseDown_in);
        init(pinId);
        init(pinNormalized);
        init(pinMouseDown);

        pinNormalized_in.onUpdate = [this](PinBase*)
            {
                pinNormalized = pinNormalized_in.value;
            };
    }

    ReturnCode initialize() override
    {
        auto paramHost = editorHost.as<gmpi::api::IParameterSetter_x>();
        if (paramHost)
        {
            int32_t paramHandle{};
            paramHost->getParameterHandle(0, paramHandle);
            pinId = paramHandle;
        }

        return PluginEditorNoGui::initialize();
    }
};

namespace
{
auto r3 = gmpi::Register<PatchMemGet>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: PatchMemGet" name="Value Get" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <Parameters>
      <Parameter id="0" datatype="float" />
    </Parameters>
    <GUI>
<!--       <Pin name="ID-in" datatype="int" parameterId="0" parameterField="Handle"/> -->
      <Pin name="Normalized-in" datatype="float" parameterId="0" parameterField="Normalized"/>
      <Pin name="MouseDown-in" datatype="bool" parameterId="0" parameterField="Grab"/>
      <Pin name="ID" datatype="int" direction="out"/>
      <Pin name="Normalized" datatype="float" direction="out"/>
      <Pin name="MouseDown" datatype="bool" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}
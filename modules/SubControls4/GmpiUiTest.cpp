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

class PluginEditorNoGuiUniDirectional : public PluginEditorBase, public gmpi::api::IEditor2_x
{
public:
    ReturnCode process() override
    {
        return ReturnCode::NoSupport;
    }

    ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
    {
        GMPI_QUERYINTERFACE(gmpi::api::IEditor);
        GMPI_QUERYINTERFACE(gmpi::api::IEditor2_x);
        return ReturnCode::NoSupport;
    }
    GMPI_REFCOUNT;
};

class PatchMemSet final : public PluginEditorNoGuiUniDirectional
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
/*
        pinNormalized.onUpdate = [this](PinBase*)
            {
                recalc();
            };
        pinValue.onUpdate = [this](PinBase*)
            {
				_RPTN(0, "PatchMemSet:Value %f\n", pinValue.value);
                //recalc();
            };
*/
    }
    ReturnCode initialize() override
    {
        paramHost = editorHost.as<gmpi::api::IParameterSetter_x>();
//        recalc();
		return PluginEditorNoGuiUniDirectional::initialize();
    }

  //  void recalc()
  //  {
		//if (paramHost)
		//{
  //          paramHost->setParameter(pinId.value, gmpi::Field::Normalized, 0, sizeof(float), &pinNormalized.value);
		//}
  //  }

    ReturnCode process() override
    {
        if (paramHost)
        {
//            paramHost->setParameter(pinId.value, gmpi::Field::Value, 0, sizeof(float), &pinValue.value);
            paramHost->setParameter(pinId.value, gmpi::Field::Normalized, 0, sizeof(float), &pinNormalized.value);
            paramHost->setParameter(pinId.value, gmpi::Field::Grab, 0, sizeof(bool), &pinMouseDown.value);
        }

        return ReturnCode::Ok;
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

class PatchMemSetFloat final : public PluginEditorNoGuiUniDirectional
{
    Pin<int32_t> pinId;
    Pin<float> pinValue;

    gmpi::shared_ptr <gmpi::api::IParameterSetter_x> paramHost;

public:
    PatchMemSetFloat()
    {
        init(pinId);
        init(pinValue);
    }
    ReturnCode initialize() override
    {
        paramHost = editorHost.as<gmpi::api::IParameterSetter_x>();
        return PluginEditorNoGuiUniDirectional::initialize();
    }

    ReturnCode process() override
    {
        if (paramHost)
        {
            paramHost->setParameter(pinId.value, gmpi::Field::Value, 0, sizeof(float), &pinValue.value);
        }

        return ReturnCode::Ok;
    }
};

namespace
{
auto r6 = gmpi::Register<PatchMemSetFloat>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: PatchMemSetFloat" name="Value Set- Float" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="ID" datatype="int"/>
      <Pin name="Value" datatype="float"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

class PatchMemGet final : public PluginEditorNoGuiUniDirectional
{
//    Pin<int32_t> pinId_in;
    Pin<float> pinNormalized_in;
    Pin<bool> pinMouseDown_in;
    Pin<float> pinValue_in;

    Pin<int32_t> pinId;
    Pin<float> pinNormalized;
    Pin<bool> pinMouseDown;
    Pin<float> pinValue;

public:
    PatchMemGet()
    {
//        init(pinId_in);
        init(pinNormalized_in);
        init(pinMouseDown_in);
        init(pinValue_in);
        init(pinId);
        init(pinNormalized);
        init(pinMouseDown);
        init(pinValue);

        pinNormalized_in.onUpdate = [this](PinBase*)
            {
                pinNormalized = pinNormalized_in.value;
            };
        pinValue_in.onUpdate = [this](PinBase*)
            {
                pinValue = pinValue_in.value;
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

        return PluginEditorNoGuiUniDirectional::initialize();
    }
};

namespace
{
auto r3 = gmpi::Register<PatchMemGet>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: PatchMemGet" name="Value" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <Parameters>
      <Parameter id="0" datatype="float" />
    </Parameters>
    <GUI>
      <Pin name="Normalized-in" datatype="float" parameterId="0" parameterField="Normalized"/>
      <Pin name="MouseDown-in" datatype="bool" parameterId="0" parameterField="Grab"/>
      <Pin name="Value-in" datatype="float" parameterId="0" parameterField="Value"/>
      <Pin name="ID" datatype="int" direction="out"/>
      <Pin name="Normalized" datatype="float" direction="out"/>
      <Pin name="MouseDown" datatype="bool" direction="out"/>
      <Pin name="Value" datatype="float" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}


// TODO timer to simulate syncronous updates
class PatchMemUpdateFloatText final : public PluginEditorNoGuiUniDirectional
{
    Pin<std::string> text_orig;
    Pin<std::string> text_mod;
    Pin<float> pinValue_in;
    Pin<float> pinValue;

public:
    PatchMemUpdateFloatText()
    {
        init(text_orig);
        init(text_mod);
        init(pinValue_in);
        init(pinValue);

#if 0
        // pass-thru
        pinValue_in.onUpdate = [this](PinBase*)
            {
                pinValue = pinValue_in.value;
            };
        text_mod.onUpdate = [this](PinBase*)
            {
                // if user changed the text at all, update the float value.
                if (text_mod.value != text_orig.value)
                {
                    pinValue = (float)strtod(text_mod.value.c_str(), 0);
                }

//                _RPTN(0, "PatchMemUpdateFloatText:Value %s\n", text_mod.value.c_str());
            };
#endif
    }

    ReturnCode process() override
	{
        if (text_mod.value != text_orig.value)
        {
            pinValue = (float)strtod(text_mod.value.c_str(), 0);
        }
        else
        {
            pinValue = pinValue_in.value;
        }

		return ReturnCode::Ok;
    }
};

namespace
{
auto r4 = gmpi::Register<PatchMemUpdateFloatText>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: PatchMemUpdateFloatText" name="PatchMemUpdateFloatText" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Original Text" datatype="string"/>
      <Pin name="Modified Text" datatype="string"/>
      <Pin name="Value" datatype="float"/>
      <Pin name="Value" datatype="float" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// just to help with simulating mono-directional system
class OneWayFloat final : public PluginEditorNoGuiUniDirectional
{
    Pin<float> pinValue_in;
    Pin<float> pinValue;

public:
    OneWayFloat()
    {
        init(pinValue_in);
        init(pinValue);

        // pass-thru
        pinValue_in.onUpdate = [this](PinBase*)
            {
                pinValue = pinValue_in.value;
            };
    }
};

namespace
{
auto r5 = gmpi::Register<OneWayFloat>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: OneWayFloat" name="OneWayFloat" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Value" datatype="float"/>
      <Pin name="Value" datatype="float" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

class OneWayText final : public PluginEditorNoGuiUniDirectional
{
    Pin<std::string> pinValue_in;
    Pin<std::string> pinValue;

public:
    OneWayText()
    {
        init(pinValue_in);
        init(pinValue);

        // pass-thru
        pinValue_in.onUpdate = [this](PinBase*)
            {
                pinValue = pinValue_in.value;
            };
    }
};

namespace
{
auto r7 = gmpi::Register<OneWayText>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: OneWayText" name="OneWayText" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Value" datatype="string"/>
      <Pin name="Value" datatype="string" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}
#include <optional>
#include <algorithm>
#include <format>
#include <filesystem>
#include <charconv>
#define _USE_MATH_DEFINES
#include <math.h>
#include "helpers/GmpiPluginEditor.h"
#include "helpers/GmpiPluginEditor2.h"
#include "helpers/CachedBlur.h"
#include "helpers/AnimatedBitmap.h"
#include "helpers/SvgParser.h"
#include "NumberEditClient.h"
#include "Extensions/EmbeddedFile.h"
#include "../shared/unicode_conversion.h"

using namespace gmpi;
using namespace gmpi::editor2;
using namespace gmpi::drawing;

SE_DECLARE_INIT_STATIC_FILE(GmpiUiTest)

class GmpiUiTest : public gmpi::editor::PluginEditor, public SsgNumberEditClient
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

struct PatchMemSet final : public PluginEditorNoGui
{
    Pin<int32_t> pinId;
    Pin<float> pinNormalized;
    Pin<bool> pinMouseDown;

	gmpi::shared_ptr <gmpi::api::IParameterSetter> paramHost;

    ReturnCode initialize() override
    {
        paramHost = editorHost.as<gmpi::api::IParameterSetter>();
		return PluginEditorBase::initialize();
    }

    ReturnCode process() override
    {
        if (paramHost)
        {
            const float safeValue = std::clamp(pinNormalized.value, 0.0f, 1.0f);

            paramHost->setParameter(pinId.value, gmpi::Field::Normalized, 0, sizeof(float), (const uint8_t*) &safeValue);
            paramHost->setParameter(pinId.value, gmpi::Field::Grab, 0, sizeof(bool), (const uint8_t*) &pinMouseDown.value);
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

struct PatchMemSetFloat final : public PluginEditorNoGui
{
    Pin<int32_t> pinId;
    Pin<float> pinValue;

    gmpi::shared_ptr <gmpi::api::IParameterSetter> paramHost;

    ReturnCode initialize() override
    {
        paramHost = editorHost.as<gmpi::api::IParameterSetter>();
        return PluginEditorBase::initialize();
    }

    ReturnCode process() override
    {
        if (paramHost)
        {
            paramHost->setParameter(pinId.value, gmpi::Field::Value, 0, sizeof(float), (const uint8_t*) &pinValue.value);
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

struct PatchMemGet final : public PluginEditorNoGui
{
    Pin<float> pinNormalized_in;
    Pin<bool> pinMouseDown_in;
    Pin<float> pinValue_in;

    Pin<int32_t> pinId;
    Pin<float> pinNormalized;
    Pin<bool> pinMouseDown;
    Pin<float> pinValue;

    ReturnCode initialize() override
    {
        if (auto paramHost = editorHost.as<gmpi::api::IParameterSetter>(); paramHost)
        {
            int32_t paramHandle{};
            paramHost->getParameterHandle(0, paramHandle);
            pinId = paramHandle;
        }

        return PluginEditorBase::initialize();
    }

    ReturnCode process() override
    {
        pinValue = pinValue_in.value;
        pinMouseDown = pinMouseDown_in.value;
        pinNormalized = pinNormalized_in.value;

        return ReturnCode::Ok;
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


struct PatchMemUpdateFloatText final : public PluginEditorNoGui
{
    Pin<std::string> text_orig;
    Pin<std::string> text_mod;
    Pin<float> pinValue_in;
    Pin<float> pinValue;

    ReturnCode process() override
	{
        if (text_mod.value != text_orig.value)
            pinValue = (float)strtod(text_mod.value.c_str(), 0);
        else
            pinValue = pinValue_in.value;

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
struct OneWayFloat final : public PluginEditorNoGui
{
    Pin<float> pinValue_in;
    Pin<float> pinValue;

    ReturnCode process() override
    {
        pinValue = pinValue_in.value;

        return ReturnCode::Ok;
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

struct OneWayText final : public PluginEditorNoGui
{
    Pin<std::string> pinValue_in;
    Pin<std::string> pinValue;

    ReturnCode process() override
    {
        pinValue = pinValue_in.value;

        return ReturnCode::Ok;
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

struct Image4Gui : public PluginEditor
{
    Pin<std::string> pinFilename;
    Pin<float> pinAnimationPosition;
    Pin<int32_t> pinFrame;
    Pin<bool> pinHdMode;

    gmpi_helper::AnimatedBitmap image;

    Image4Gui()
    {
        pinFilename.onUpdate = [this](PinBase*) { onSetFilename(); };
		pinAnimationPosition.onUpdate = [this](PinBase*) { calcDrawAt(); };
		pinFrame.onUpdate = [this](PinBase*) { calcDrawAt(); };
    }

    void setAnimationPos(float p)
    {
        pinAnimationPosition = p;
    }

    void onSetFilename()
    {
        // ensure that the filename has an extension that indicates that it is an image (SynthEdit has special rules for locating images).
		std::filesystem::path uri(pinFilename.value);
		if (!uri.has_extension())
		{
			uri.replace_extension(".png");
		}

        // get drawingFactory
        gmpi::shared_ptr<gmpi::api::IUnknown> ret;
        drawingHost->getDrawingFactory(ret.put());
        auto drawingFactory = ret.as<drawing::api::IFactory>();

        auto synthEdit = drawingHost.as<synthedit::IEmbeddedFileSupport>();
        
        ReturnString fullUri;
        synthEdit->findResourceUri(uri.generic_string().c_str(), &fullUri);
		synthEdit->registerResourceUri(fullUri.c_str());

        // images have a coresponding text file that provides information about how they should be animated.
        std::filesystem::path textfileUri(fullUri.str());
		textfileUri.replace_extension("txt");
        synthEdit->registerResourceUri(textfileUri.generic_string().c_str());

        if (MP_OK == image.load(drawingFactory.get(), fullUri.c_str(), textfileUri.generic_string().c_str()))
        {
            onLoaded();

            const auto currrentSize = getSize(bounds);
            if (currrentSize.width) // already sized?
            {
                gmpi::drawing::Size avail{10000,10000};
                gmpi::drawing::Size desired{};
                measure(&avail, &desired);

                if(currrentSize != desired)
                    drawingHost->invalidateMeasure();
            }

            calcDrawAt();
        }
    }

    void onLoaded()
    {
        //if (image.metadata())
        //{
        //    int fc = image.metadata()->getFrameCount();
        //    pinFrameCount = fc;
        //}
    }

    void calcDrawAt()
    {
        if (pinFrame.value >= 0)
        {
            if (image.calcFrame(pinFrame.value))
                drawingHost->invalidateRect({});
        }
        else
        {
            if (image.calcDrawAt(pinAnimationPosition.value))
                drawingHost->invalidateRect({});
        }
    }

    ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
    {
        Graphics g(drawingContext);

        if (pinHdMode.value)
        {
            const auto originalTransform = g.getTransform();
            const auto adjustedTransform = makeScale(0.5f) * originalTransform;
            g.setTransform(adjustedTransform);

            image.renderBitmap(g, { 0, 0 });

            g.setTransform(originalTransform);
        }
        else
        {
            image.renderBitmap(g, { 0, 0 });
        }

        return ReturnCode::Ok;
    }

    ReturnCode measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize) override
    {
        if (image.metadata())
        {
            *returnDesiredSize = image.metadata()->getPaddedFrameSize();

            if (pinHdMode.value)
            {
                returnDesiredSize->width /= 2;
                returnDesiredSize->height /= 2;
            }
        }
        else
        {
            returnDesiredSize->width = 10;
            returnDesiredSize->height = 10;
        }

        return ReturnCode::Ok;
    }

    ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override
    {
        return ReturnCode::Ok; // TODO image.bitmapHitTestLocal(point) ? ReturnCode::Ok : ReturnCode::Fail;
    }
};

// Register the GUI
//SE_DECLARE_INIT_STATIC_FILE(Image4_Gui);
namespace
{
auto r8 = gmpi::Register<Image4Gui>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: Image4" name="Image4" category="GMPI/SDK Examples" vendor="Jeff McClintock">
	<GUI graphicsApi="GmpiGui">
		<Pin name="Filename" datatype="string_utf8" default="knob_sm" isFilename="true" metadata="bmp" />
		<Pin name="Animation Position" datatype="float" default="-1" />
		<Pin name="Frame" datatype="int" default="-1" />
		<Pin name="HD" datatype="bool" isMinimised="true"/>
	</GUI>
  </Plugin>
</PluginList>
)XML");
}

class TextEntry4Gui : public PluginEditor
{
protected:
    Pin<std::string> pinValueIn;
    Pin<std::string> pinValueOut;

    sdk::TextEditCallback callback;

public:
    TextEntry4Gui()
    {
        callback.callback = [this](ReturnCode result)
            {
                pinValueOut = callback.text;
            };
    }

    ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
    {
        Graphics g(drawingContext);

		auto borderBrush = g.createSolidColorBrush(Colors::White);
		auto textBrush = g.createSolidColorBrush(Colors::Black);
		auto textRect = bounds;
		textRect.left += 2;
		textRect.top += 2;
		textRect.right -= 2;
		textRect.bottom -= 2;

		auto textFormat = g.getFactory().createTextFormat(getHeight(textRect));

		g.fillRectangle(textRect, borderBrush);
		g.drawTextU(pinValueIn.value, textFormat, textRect, textBrush);
		g.drawRectangle(textRect, borderBrush);
        return ReturnCode::Ok;
    }

    ReturnCode process() override
    {
        pinValueOut = pinValueIn.value;
        drawingHost->invalidateRect({});

        //        if (pinValue.value)
//            editorHost2->setDirty(); // return to zero next frame.

        return ReturnCode::Ok;
    }

    ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override
    {
        return ReturnCode::Ok;
    }
    gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override
    {
		inputHost->setCapture();
        return ReturnCode::Unhandled;
    }
    gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override
    {
        inputHost->releaseCapture();

		gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
        dialogHost->createTextEdit(&bounds, unknown.put());

		auto textEdit = unknown.as<gmpi::api::ITextEdit>();

        if (textEdit)
        {
            textEdit->setText(pinValueIn.value.c_str());
            textEdit->showAsync(&callback);
        }

        return ReturnCode::Unhandled;
    }
};

// Register the GUI
namespace
{
auto r8B = gmpi::Register<TextEntry4Gui>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: TextEntry4Gui" name="Text Entry" category="GMPI/SDK Examples" vendor="Jeff McClintock">
	<GUI graphicsApi="GmpiGui">
		<Pin name="Value" datatype="string_utf8" />
		<Pin name="Value" datatype="string_utf8" direction="out" />
	</GUI>
  </Plugin>
</PluginList>
)XML");
}

class MouseTarget : public PluginEditor
{
protected:
    Pin<bool>  pinHover;
    Pin<bool>  pinLClick;
    Pin<float> pinX;
    Pin<float> pinY;

public:
    gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override
    {
        inputHost->setCapture();
        pinLClick = true;

        return gmpi::ReturnCode::Ok;
    }

    gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override
    {
        bool isCaptured{};
		//inputHost->getCapture(isCaptured);
  //      if (isCaptured)
        {
            pinX = point.x;
            pinY = point.y;
        }

        return gmpi::ReturnCode::Ok;
    }

    gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override
    {
        pinLClick = false;
        inputHost->releaseCapture();
        return gmpi::ReturnCode::Ok;
    }

    gmpi::ReturnCode setHover(bool isMouseOverMe) override
    {
        pinHover = isMouseOverMe;

        return gmpi::ReturnCode::Unhandled;
    }
};

// Register the GUI
//SE_DECLARE_INIT_STATIC_FILE(Image4_Gui);
namespace
{
auto r9 = gmpi::Register<MouseTarget>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: MouseTarget" name="MouseTarget" category="GMPI/SDK Examples" vendor="Jeff McClintock">
	<GUI graphicsApi="GmpiGui">
        <Pin name="Hover" datatype="bool" direction="out"/>
        <Pin name="Left Click" datatype="bool" direction="out"/>
        <Pin name="X" datatype="float" direction="out"/>
        <Pin name="Y" datatype="float" direction="out"/>
	</GUI>
  </Plugin>
</PluginList>
)XML");
}

class Delta final : public PluginEditorNoGui
{
    Pin<float> pinValue_in;
    Pin<float> pinValue;

    float prev{};

public:
    ReturnCode process() override
    {
    //    _RPTN(0, "Delta: %f -> %f delta=%f\n", prev, pinValue_in.value, pinValue_in.value - prev);

        pinValue = pinValue_in.value - prev;
		prev = pinValue_in.value;

        if(pinValue.value)
            editorHost2->setDirty(); // return to zero next frame.

        return ReturnCode::Ok;
    }
};

namespace
{
auto r10 = gmpi::Register<Delta>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: Delta" name="Delta" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Value" datatype="float"/>
      <Pin name="Value" datatype="float" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

class Add final : public PluginEditorNoGui
{
    Pin<float> pinValue_inA;
    Pin<float> pinValue_inB;
    Pin<float> pinValue;

public:
    ReturnCode process() override
    {
        pinValue = pinValue_inA.value + pinValue_inB.value;
        return ReturnCode::Ok;
    }
};

namespace
{
auto r11 = gmpi::Register<Add>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: Add" name="Add" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Value" datatype="float"/>
      <Pin name="Value" datatype="float"/>
      <Pin name="Value" datatype="float" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct Multiply final : public PluginEditorNoGui
{
    Pin<float> pinValue_inA;
    Pin<float> pinValue_inB;
    Pin<float> pinValue;

    ReturnCode process() override
    {
        pinValue = pinValue_inA.value * pinValue_inB.value;
        return ReturnCode::Ok;
    }
};

namespace
{
auto r12 = gmpi::Register<Multiply>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: Multiply" name="Multiply" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Value" datatype="float"/>
      <Pin name="Value" datatype="float"/>
      <Pin name="Value" datatype="float" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct Bool2Float final : public PluginEditorNoGui
{
    Pin<bool> pinValue_in;
    Pin<float> pinValue;

    ReturnCode process() override
    {
        pinValue = pinValue_in.value ? 1.0f : 0.0f;
        return ReturnCode::Ok;
    }
};

namespace
{
auto r13 = gmpi::Register<Bool2Float>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: Bool2Float" name="Bool2Float" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Value" datatype="bool"/>
      <Pin name="Value" datatype="float" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct Float2Text final : public PluginEditorNoGui
{
    Pin<float> pinValue_in;
    Pin<int> pinDecimals;
    Pin<std::string> pinOutput;

    ReturnCode process() override
    {
        int decimals = pinDecimals.value;

        if (decimals < 0) // -1 : automatic decimal places.
        {
            float absolute = fabsf(pinValue_in.value);
            decimals = 2;

            if (absolute < 0.1f)
            {
                if (absolute == 0.0f)
                    decimals = 1;
                else
                    decimals = 4;
            }
            else
                if (absolute > 10.f)
                    decimals = 1;
                else
                    if (absolute > 100.f)
                        decimals = 0;
        }

        // longest float value is about 40 characters.
        const int maxSize = 50;

        char formatString[maxSize];

        // Use safe printf if available.
#if defined(_MSC_VER)
        sprintf_s(formatString, maxSize, "%%.%df", decimals);
#else
        sprintf(formatString, "%%.%df", decimals);
#endif

        char outputString[maxSize];

        //#if defined(_MSC_VER)
        //	swprintf_s( outputString, maxSize, formatString, (double) (float) inputValue );
        //#else
        snprintf(outputString, maxSize, formatString, (double)pinValue_in.value);
        //	#endif

            // Replace -0.0 with 0.0 ( same for -0.00 and -0.000 etc).
            // deliberate 'feature' of printf is to round small negative numbers to -0.0
        if (outputString[0] == '-' && (float)pinValue_in.value > -1.0f)
        {
            int i = (int)strlen(outputString) - 1;
            while (i > 0)
            {
                if (outputString[i] != '0' && outputString[i] != '.')
                {
                    break;
                }
                --i;
            }
            if (i == 0) // nothing but zeros (or dot). remove leading minus sign.
            {
                strcpy(outputString, outputString + 1);
            }
        }

        pinOutput = outputString;

        return ReturnCode::Ok;
    }
};

namespace
{
auto r14 = gmpi::Register<Float2Text>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: Float2Text" name="Float2Text" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Value" datatype="float"/>
      <Pin name="Decimal Places" datatype="int" default="-1"/>
      <Pin name="Value" datatype="string_utf8" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct Utf82Wide final : public PluginEditorNoGui
{
    Pin<std::string> pinValue_in;
    Pin<std::wstring> pinOutput;

    ReturnCode process() override
    {
        pinOutput = JmUnicodeConversions::Utf8ToWstring(pinValue_in.value);
        return ReturnCode::Ok;
    }
};

namespace
{
auto r15 = gmpi::Register<Utf82Wide>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: Utf82Wide" name="Utf82Wide" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Value" datatype="string_utf8"/>
      <Pin name="Value" datatype="string" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct Wide2Utf8 final : public PluginEditorNoGui
{
    Pin<std::wstring> pinValue_in;
    Pin<std::string> pinOutput;

    ReturnCode process() override
    {
        pinOutput = JmUnicodeConversions::WStringToUtf8(pinValue_in.value);
        return ReturnCode::Ok;
    }
};

namespace
{
auto r16 = gmpi::Register<Wide2Utf8>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: Wide2Utf8" name="Wide2Utf8" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Value" datatype="string"/>
      <Pin name="Value" datatype="string_utf8" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct GraphicsProcessor : public PluginEditorNoGui
{
    gmpi::shared_ptr<gmpi::api::IDrawingHost> drawingHost;
    Factory drawingFactory;

    ReturnCode setHost(gmpi::api::IUnknown* phost) override
    {
        gmpi::shared_ptr<gmpi::api::IUnknown> unknown(phost);
        drawingHost = unknown.as<gmpi::api::IDrawingHost>();

        gmpi::shared_ptr<gmpi::api::IUnknown> unknown2;
        drawingHost->getDrawingFactory(unknown2.put());
        unknown2->queryInterface(&drawing::api::IFactory::guid, AccessPtr::put_void(drawingFactory));

        return PluginEditorNoGui::setHost(phost);
    }
};


struct CircleGeometry final : public GraphicsProcessor
{
    Pin<int64_t> pinOutput;

    gmpi::drawing::PathGeometry geometry;

  //  ~CircleGeometry()
  //  {
		//_RPT0(0, "CircleGeometry destructor\n");
  //  }

    ReturnCode process() override
    {
        if (!pinOutput.value)
        {
            geometry = drawingFactory.createPathGeometry();
#ifdef _DEBUG
            // test move/copy operators.
            gmpi::drawing::PathGeometry geometry2;
            geometry2 = geometry;

            gmpi::drawing::PathGeometry geometry3(geometry);

//            std::shared_ptr<int> test;
#endif

            auto sink = geometry.open();

            // make a circle from two half-circle arcs
            const Point center{ 20.0f, 20.0f };
            const float radius{ 10.f };

            constexpr float pi = static_cast<float>(M_PI);
            sink.beginFigure({ center.x, center.y - radius }, drawing::FigureBegin::Filled);
			ArcSegment arc1{ { center.x, center.y + radius}, { radius, radius }, pi, SweepDirection::Clockwise, ArcSize::Small };
            ArcSegment arc2{ { center.x, center.y - radius}, { radius, radius }, pi, SweepDirection::Clockwise, ArcSize::Small };
            sink.addArc(arc1);
            sink.addArc(arc2);

            sink.endFigure(FigureEnd::Closed);
            sink.close();

			pinOutput = reinterpret_cast<int64_t>(AccessPtr::get(geometry));
        }

        return ReturnCode::Ok;
    }
};

namespace
{
auto r17 = gmpi::Register<CircleGeometry>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: CircleGeometry" name="CircleGeometry" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Path" datatype="int64" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct SvgGeometry final : public GraphicsProcessor
{
    Pin<std::string> pinFilename;
    Pin<int64_t> pinOutput;

    gmpi::drawing::PathGeometry geometry;

    ReturnCode process() override
    {
        if (!pinOutput.value)
        {
            geometry = drawingFactory.createPathGeometry();

            auto sink = geometry.open();

            SvgParser::parseToGeometry(pinFilename.value, AccessPtr::get(sink));

            //// make a circle from two half-circle arcs
            //const Point center{ 20.0f, 20.0f };
            //const float radius{ 10.f };

            //constexpr float pi = static_cast<float>(M_PI);
            //sink.beginFigure({ center.x, center.y - radius }, drawing::FigureBegin::Filled);
            //ArcSegment arc1{ { center.x, center.y + radius}, { radius, radius }, pi, SweepDirection::Clockwise, ArcSize::Small };
            //ArcSegment arc2{ { center.x, center.y - radius}, { radius, radius }, pi, SweepDirection::Clockwise, ArcSize::Small };
            //sink.addArc(arc1);
            //sink.addArc(arc2);

            //sink.endFigure(FigureEnd::Closed);
            sink.close();

            pinOutput = reinterpret_cast<int64_t>(AccessPtr::get(geometry));
        }

        return ReturnCode::Ok;
    }
};

namespace
{
auto r17b = gmpi::Register<SvgGeometry>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: SvgGeometry" name="Svg Geometry" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
	  <Pin name="SVG File" datatype="string_utf8" isFilename="true" metadata="svg"/>
      <Pin name="Path" datatype="int64" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct RenderGeometry final : public PluginEditor
{
    Pin<int64_t> pinInput;

    ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
    {
		if (!pinInput.value)
			return ReturnCode::Ok;

        PathGeometry geometry;
        {
            auto unknown = reinterpret_cast<gmpi::api::IUnknown*>(pinInput.value);
            if (ReturnCode::Ok != unknown->queryInterface(&drawing::api::IPathGeometry::guid, AccessPtr::put_void(geometry)))
                return ReturnCode::Fail;
        }

        Graphics g(drawingContext);

		auto brush = g.createSolidColorBrush(Colors::White);
		auto strokeStyle = g.getFactory().createStrokeStyle(CapStyle::Flat);

		g.drawGeometry(geometry, brush);

        return ReturnCode::Ok;
    }
};

namespace
{
auto r18 = gmpi::Register<RenderGeometry>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: RenderGeometry" name="Render" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Path" datatype="int64"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct Render2Bitmap final : public GraphicsProcessor
{
    Pin<int64_t> pinInput;
    Pin<int64_t> pinOutput;

    Bitmap bitmap;

    ReturnCode process() override
    {
        if (!pinInput.value)
            return ReturnCode::Ok;

        PathGeometry geometry;
        {
            auto unknown = reinterpret_cast<gmpi::api::IUnknown*>(pinInput.value);
            if (ReturnCode::Ok != unknown->queryInterface(&drawing::api::IPathGeometry::guid, AccessPtr::put_void(geometry)))
                return ReturnCode::Fail;
        }
        {
            const SizeU size{ 100, 100 };// getWidth(bounds), getHeight(bounds)}; size is zero on first process.
            const int32_t flags = (int32_t)BitmapRenderTargetFlags::Mask;

            // get factory.
            Factory factory;
            {
                gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
                drawingHost->getDrawingFactory(unknown.put());
                unknown->queryInterface(&drawing::api::IFactory::guid, AccessPtr::put_void(factory));
            }

            // create a bitmap render target on CPU.
            auto g2 = factory.createCpuRenderTarget(size, flags);

            g2.beginDraw();

            auto brush = g2.createSolidColorBrush(Colors::White);
            auto strokeStyle = factory.createStrokeStyle(CapStyle::Flat);

            g2.drawGeometry(geometry, brush);

            g2.endDraw();

            bitmap = g2.getBitmap();
        }

        pinOutput = reinterpret_cast<int64_t>(AccessPtr::get(bitmap));

        return ReturnCode::Ok;
    }
};

namespace
{
auto r19 = gmpi::Register<Render2Bitmap>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: Render2Bitmap" name="Render2Bitmap" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Path" datatype="int64"/>
      <Pin name="Bitmap" datatype="int64" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct RenderBitmap final : public PluginEditor
{
    Pin<int64_t> pinInput;

    ReturnCode process() override
    {
        drawingHost->invalidateRect(&bounds);
        return ReturnCode::Ok;
    }

    ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
    {
        if (!pinInput.value)
            return ReturnCode::Ok;
        
        Graphics g(drawingContext);

        Bitmap bitmap;
        {
            auto unknown = reinterpret_cast<gmpi::api::IUnknown*>(pinInput.value);
            if (ReturnCode::Ok != unknown->queryInterface(&drawing::api::IBitmap::guid, AccessPtr::put_void(bitmap)))
                return ReturnCode::Fail;
        }

        const auto size = bitmap.getSize();
        const Rect rect{ 0, 0, static_cast<float>(size.width), static_cast<float>(size.height) };

		g.drawBitmap(bitmap, rect, rect);

        return ReturnCode::Ok;
    }
};

namespace
{
auto r20 = gmpi::Register<RenderBitmap>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: RenderBitmap" name="RenderBitmap" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Bitmap" datatype="int64"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct BlurBitmap final : public GraphicsProcessor
{
    Pin<int64_t> pinInput;
    Pin<int64_t> pinOutput;

    drawing::Color tint = drawing::colorFromHex(0xd4c1ffu);
    Bitmap blurredBitmap;


    ReturnCode process() override
    {
        if (!pinInput.value)
            return ReturnCode::Ok;

        auto unknown = reinterpret_cast<gmpi::api::IUnknown*>(pinInput.value);

        Bitmap bitmap;
        PathGeometry geometry;

        // test if input is Bitmap or Path
        if (ReturnCode::Ok != unknown->queryInterface(&drawing::api::IBitmap::guid, AccessPtr::put_void(bitmap)))
        {
            if (ReturnCode::Ok != unknown->queryInterface(&drawing::api::IPathGeometry::guid, AccessPtr::put_void(geometry)))
                return ReturnCode::Fail;

            // it's a path, render it to the mask bitmap.
            const SizeU size{ 100, 100 };// getWidth(bounds), getHeight(bounds)}; size is zero on first process.
            const int32_t flags = (int32_t)BitmapRenderTargetFlags::Mask;

            // get factory.
            Factory factory;
            {
                gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
                drawingHost->getDrawingFactory(unknown.put());
                unknown->queryInterface(&drawing::api::IFactory::guid, AccessPtr::put_void(factory));
            }

            // create a bitmap render target on CPU.
            auto g2 = factory.createCpuRenderTarget(size, flags);

            g2.beginDraw();

            auto brush = g2.createSolidColorBrush(Colors::White);
            auto strokeStyle = factory.createStrokeStyle(CapStyle::Flat);

            g2.drawGeometry(geometry, brush);

            g2.endDraw();

            bitmap = g2.getBitmap();
        }

        auto size = bitmap.getSize();
        const Rect rect{ 0, 0, static_cast<float>(size.width), static_cast<float>(size.height) };

        // copy the mask bitmap to working RAM.
        std::vector<uint8_t> workingArea;
        {
            auto data = bitmap.lockPixels();

            const auto stride = data.getBytesPerRow();
			const auto format = data.getPixelFormat();

            constexpr int pixelSize = 1; // 8 bytes per pixel for half-float,1 byte for mask.
            const int totalPixels = (int)size.height * stride / pixelSize;

			const uint8_t* pixel = data.getAddress();

            // could switch on format here
            workingArea.resize(totalPixels);
            for (int i = 0; i < totalPixels; ++i)
            {
                workingArea[i] = *pixel++;//[i * pixelSize + 3]; // alpha channel
            }
        }
        // modify the buffer
        {
            // modify pixels here
#if 0
            {
                // half-float pixels.
                auto pixel = (half*)data.getAddress();
                ginARGB(pixel, imageSize.width, imageSize.height, 5);
            }
#else
            {
                // create a blurred mask of the image.
                auto pixel = workingArea.data();
                ginSingleChannel(pixel, size.width, size.height, 5);
            }
#endif

            // get drawingFactory
            Factory factory;
            {
                gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
                drawingHost->getDrawingFactory(unknown.put());
                unknown->queryInterface(&drawing::api::IFactory::guid, AccessPtr::put_void(factory));
            }

            // create bitmap
            blurredBitmap = factory.createImage(size, (int32_t)drawing::BitmapRenderTargetFlags::EightBitPixels| (int32_t)drawing::BitmapRenderTargetFlags::CpuReadable);
            {
                auto destdata = blurredBitmap.lockPixels(drawing::BitmapLockFlags::Write);
                constexpr int pixelSize = 4; // 8 bytes per pixel for half-float, 4 for 8-bit
                auto stride = destdata.getBytesPerRow();
                auto format = destdata.getPixelFormat();
                const int totalPixels = (int)size.height * stride / pixelSize;

                const int pixelSizeTest = stride / size.width; // 8 for half-float RGB, 4 for 8-bit sRGB, 1 for alpha mask

                auto pixelsrc = workingArea.data(); // data.getAddress();
                //   auto pixeldest = (half*)destdata.getAddress();
                auto pixeldest = destdata.getAddress();

                float tintf[4] = { tint.r, tint.g, tint.b, tint.a };

                constexpr float inv255 = 1.0f / 255.0f;

                for (int i = 0; i < totalPixels; ++i)
                {
                    const auto alpha = *pixelsrc;
                    if (alpha == 0)
                    {
                        pixeldest[0] = pixeldest[1] = pixeldest[2] = pixeldest[3] = {};
                    }
                    else
                    {
                        const float AlphaNorm = alpha * inv255;
                        for (int j = 0; j < 3; ++j)
                        {
                            // To linear
                            auto cf = tintf[j];

                            // pre-multiply in linear space.
                            cf *= AlphaNorm;

                            // back to SRGB
                            pixeldest[j] = drawing::linearPixelToSRGB(cf);
                        }
                        pixeldest[3] = alpha;
                    }

                    pixelsrc++;
                    pixeldest += 4;
                }
            }
        }

        pinOutput = reinterpret_cast<int64_t>(AccessPtr::get(blurredBitmap));

        return ReturnCode::Ok;
    }
};

namespace
{
auto r21 = gmpi::Register<BlurBitmap>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: BlurBitmap" name="Blur" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Bitmap/Path" datatype="int64"/>
      <Pin name="Blurred" datatype="int64" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct TextFormatNode final : public GraphicsProcessor
{
    Pin<int64_t> pinOutput;

    gmpi::drawing::TextFormat textFormat;

    ReturnCode process() override
    {
        if (!pinOutput.value)
        {
			textFormat = drawingFactory.createTextFormat(20.0f);

            pinOutput = reinterpret_cast<int64_t>(AccessPtr::get(textFormat));
        }

        return ReturnCode::Ok;
    }
};

namespace
{
auto r22 = gmpi::Register<TextFormatNode>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: TextFormat" name="TextFormat" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Font" datatype="int64" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct RenderText2Bitmap final : public GraphicsProcessor
{
    Pin<int64_t> pinInput;
    Pin<std::string> pinText;
    Pin<int64_t> pinOutput;

    Bitmap bitmap;

    ReturnCode process() override
    {
        if (!pinInput.value)
            return ReturnCode::Ok;

        TextFormat geometry;
        {
            auto unknown = reinterpret_cast<gmpi::api::IUnknown*>(pinInput.value);
            if (ReturnCode::Ok != unknown->queryInterface(&drawing::api::ITextFormat::guid, AccessPtr::put_void(geometry)))
                return ReturnCode::Fail;
        }
        {
            const SizeU size{ 100, 100 };// getWidth(bounds), getHeight(bounds)}; size is zero on first process.
            const int32_t flags = (int32_t)BitmapRenderTargetFlags::Mask;

            // get factory.
            Factory factory;
            {
                gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
                drawingHost->getDrawingFactory(unknown.put());
                unknown->queryInterface(&drawing::api::IFactory::guid, AccessPtr::put_void(factory));
            }

            // create a bitmap render target on CPU.
            auto g2 = factory.createCpuRenderTarget(size, flags);

            g2.beginDraw();

            auto brush = g2.createSolidColorBrush(Colors::White);
            auto strokeStyle = factory.createStrokeStyle(CapStyle::Flat);

            //g2.drawGeometry(geometry, brush);
			
            g2.drawTextU(pinText.value, geometry, { 0, 0, 100, 100 }, brush);

            g2.endDraw();

            bitmap = g2.getBitmap();
        }

        pinOutput = reinterpret_cast<int64_t>(AccessPtr::get(bitmap));

        return ReturnCode::Ok;
    }
};

namespace
{
auto r23 = gmpi::Register<RenderText2Bitmap>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: RenderText2Bitmap" name="RenderText2Bitmap" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Font" datatype="int64"/>
      <Pin name="Text" datatype="string_utf8"/>
      <Pin name="Bitmap" datatype="int64" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// 8-bit mask image to 32-bit bitmap.
struct Mask2Bitmap final : public GraphicsProcessor
{
    Pin<int64_t> pinInput;
    Pin<int64_t> pinOutput;

    drawing::Color tint = drawing::colorFromHex(0xffffffu);
    Bitmap blurredBitmap;

    ReturnCode process() override
    {
        if (!pinInput.value)
            return ReturnCode::Ok;

        auto unknown = reinterpret_cast<gmpi::api::IUnknown*>(pinInput.value);

        Bitmap bitmap;
        PathGeometry geometry;

        // test if input is Bitmap or Path
        if (ReturnCode::Ok != unknown->queryInterface(&drawing::api::IBitmap::guid, AccessPtr::put_void(bitmap)))
            return ReturnCode::Fail;

        auto size = bitmap.getSize();
        const Rect rect{ 0, 0, static_cast<float>(size.width), static_cast<float>(size.height) };

        // copy the mask bitmap to working RAM.
        std::vector<uint8_t> workingArea;
        {
            auto data = bitmap.lockPixels();

            const auto stride = data.getBytesPerRow();
            const auto format = data.getPixelFormat();

            constexpr int pixelSize = 1; // 8 bytes per pixel for half-float,1 byte for mask.
            const int totalPixels = (int)size.height * stride / pixelSize;

            const uint8_t* pixel = data.getAddress();

            // could switch on format here
            workingArea.resize(totalPixels);
            for (int i = 0; i < totalPixels; ++i)
            {
                workingArea[i] = *pixel++;//[i * pixelSize + 3]; // alpha channel
            }
        }
        // modify the buffer
        {
            // modify pixels here (or don't)

            // get drawingFactory
            Factory factory;
            {
                gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
                drawingHost->getDrawingFactory(unknown.put());
                unknown->queryInterface(&drawing::api::IFactory::guid, AccessPtr::put_void(factory));
            }

            // create bitmap
            blurredBitmap = factory.createImage(size, (int32_t)drawing::BitmapRenderTargetFlags::EightBitPixels | (int32_t)drawing::BitmapRenderTargetFlags::CpuReadable);
            {
                auto destdata = blurredBitmap.lockPixels(drawing::BitmapLockFlags::Write);
                constexpr int pixelSize = 4; // 8 bytes per pixel for half-float, 4 for 8-bit
                auto stride = destdata.getBytesPerRow();
                auto format = destdata.getPixelFormat();
                const int totalPixels = (int)size.height * stride / pixelSize;

                const int pixelSizeTest = stride / size.width; // 8 for half-float RGB, 4 for 8-bit sRGB, 1 for alpha mask

                auto pixelsrc = workingArea.data(); // data.getAddress();
                //   auto pixeldest = (half*)destdata.getAddress();
                auto pixeldest = destdata.getAddress();

                float tintf[4] = { tint.r, tint.g, tint.b, tint.a };

                constexpr float inv255 = 1.0f / 255.0f;

                for (int i = 0; i < totalPixels; ++i)
                {
                    const auto alpha = *pixelsrc;
                    if (alpha == 0)
                    {
                        pixeldest[0] = pixeldest[1] = pixeldest[2] = pixeldest[3] = {};
                    }
                    else
                    {
                        const float AlphaNorm = alpha * inv255;
                        for (int j = 0; j < 3; ++j)
                        {
                            // To linear
                            auto cf = tintf[j];

                            // pre-multiply in linear space.
                            cf *= AlphaNorm;

                            // back to SRGB
                            pixeldest[j] = drawing::linearPixelToSRGB(cf);
                        }
                        pixeldest[3] = alpha;
                    }

                    pixelsrc++;
                    pixeldest += 4;
                }
            }
        }

        pinOutput = reinterpret_cast<int64_t>(AccessPtr::get(blurredBitmap));

        return ReturnCode::Ok;
    }
};

namespace
{
auto r24 = gmpi::Register<Mask2Bitmap>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: Mask2Bitmap" name="Mask2Bitmap" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Bitmap8" datatype="int64"/>
      <Pin name="Bitmap24" datatype="int64" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

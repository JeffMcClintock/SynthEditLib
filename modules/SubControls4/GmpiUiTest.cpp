#if defined(__has_include)
#if __has_include(<version>)
#include <version>
#endif
#endif

#include <optional>
#include <algorithm>
#include <format>
#include "se_filesystem.h"
#include <charconv>
#define _USE_MATH_DEFINES
#include <math.h>
#include "helpers/GmpiPluginEditor.h"
#include "helpers/GmpiPluginEditor2.h"
#include "../shared/MonoDirectionalPins.h" // In<T> / Out<T> / ObjectIn<T> / ObjectOut<T>
#include "helpers/CachedBlur.h"
#include "helpers/AnimatedBitmap.h"
#include "helpers/SvgParser.h"
#include "NumberEditClient.h"
#include "Extensions/EmbeddedFile.h"
#include "../shared/unicode_conversion.h"
#include "mfc_emulation.h"

using namespace gmpi;
using namespace gmpi::editor2;
using namespace gmpi::drawing;

SE_DECLARE_INIT_STATIC_FILE(GmpiUiTest)

class GmpiUiTest : public gmpi::editor::PluginEditor, public NumberEditClient
{
    cachedBlur blur;
    NumberEdit numberEdit;
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

	ReturnCode setHost(gmpi::api::IUnknown* host) override
	{
		const auto result = PluginEditor::setHost(host);
		updateTextFromValue();
		return result;
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
		if (0 == (flags & static_cast<int32_t>(gmpi::api::PointerFlags::FirstButton))) // left button
            return ReturnCode::Unhandled;

        return inputHost->setCapture();
    }
    gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override
    {
        return ReturnCode::Unhandled;
    }
    gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override
    {
//		inputHost->getFocus();

        numberEdit.show(dialogHost, &bounds);

        return inputHost->releaseCapture();
    }
    gmpi::ReturnCode onKeyPress(wchar_t c) override
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
  <Plugin id="SE: GmpiUiTest" name="GMPI-UI Test" category="Debug" vendor="Jeff McClintock">
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
    In<int32_t> pinId;
    In<float> pinNormalized;
    In<bool> pinMouseDown;

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
            // Only push Normalized while grabbed (dragging). Writing it every block would clobber
            // other writers (a Number Entry editing the Value field, host automation) the instant
            // they change the parameter - then nothing typed would ever stick.
            if (pinMouseDown.value)
            {
                const float safeValue = std::clamp(pinNormalized.value, 0.0f, 1.0f);
                paramHost->setParameter(pinId.value, gmpi::Field::Normalized, 0, sizeof(float), (const uint8_t*) &safeValue);
            }
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
    In<int32_t> pinId;
    In<float> pinValue;

    gmpi::shared_ptr <gmpi::api::IParameterSetter> paramHost;

    ReturnCode initialize() override
    {
        paramHost = editorHost.as<gmpi::api::IParameterSetter>();
        return PluginEditorBase::initialize();
    }

    ReturnCode process() override
    {
        if (paramHost)
            paramHost->setParameter(pinId.value, gmpi::Field::Value, 0, sizeof(float), (const uint8_t*) &pinValue.value);

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
    In<float> pinNormalized_in;
    In<bool> pinMouseDown_in;
    In<float> pinValue_in;

    Out<int32_t> pinId;
    Out<float> pinNormalized;
    Out<bool> pinMouseDown;
    Out<float> pinValue;

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
    In<std::string> text_orig;
    In<std::string> text_mod;
    In<float> pinValue_in;
    Out<float> pinValue;

    ReturnCode process() override
	{
        if (text_mod.value != text_orig.value)
        {
            // Format using C++20 std::format.
            int decimals = 2; // default decimals if needed

            // Attempt to find decimals from pinValue_in or fallback
            // but no decimals input here, so fallback to 2
            pinValue = static_cast<float>(strtod(text_mod.value.c_str(), nullptr));
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
struct OneWayFloat final : public PluginEditorNoGui
{
    In<float> pinValue_in;
    Out<float> pinValue;

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
    In<std::string> pinValue_in;
    Out<std::string> pinValue;

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
    In<std::string> pinFilename;
    In<float> pinAnimationPosition;
    In<int32_t> pinFrame;
    In<bool> pinHdMode;

    gmpi_helper::AnimatedBitmap image;

    Image4Gui()
    {
        pinFilename.onUpdate = [this](PinBase*) { onSetFilename(); };
		pinAnimationPosition.onUpdate = [this](PinBase*) { calcDrawAt(); };
		pinFrame.onUpdate = [this](PinBase*) { calcDrawAt(); };
    }

    void onSetFilename()
    {
        // ensure that the filename has an extension that indicates that it is an image (SynthEdit has special rules for locating images).
		se_fs::path uri(pinFilename.value);
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
        se_fs::path textfileUri(fullUri.str());
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

// A bundle of drawing-style properties (fill, stroke, stroke width) passed as a single
// reference-counted object - the CSS idea of a 'class' applied to many elements with one wire.
// It's an interface (not a value struct) so it can grow new properties (dashes, gradients,
// caps, ...) via an IStyle2 without breaking existing modules or saved patches.
struct DECLSPEC_NOVTABLE IStyle : gmpi::api::IUnknown
{
    // Each getter returns Ok when the property is supplied. (Future: Fail could mean 'inherit',
    // for CSS-like cascading/merging.) A colour with alpha 0 means 'don't paint this part'.
    virtual gmpi::ReturnCode getFillColor(gmpi::drawing::Color* returnColor) = 0;
    virtual gmpi::ReturnCode getStrokeColor(gmpi::drawing::Color* returnColor) = 0;
    virtual gmpi::ReturnCode getStrokeWidth(float* returnWidth) = 0;
    virtual int32_t getStrokeCap() = 0; // gmpi::drawing::CapStyle as int (Flat / Square / Round)

    // {5D4686F8-97A7-4A55-AFFC-6B7CB6BA4505}
    inline static const gmpi::api::Guid guid =
    { 0x5d4686f8, 0x97a7, 0x4a55, { 0xaf, 0xfc, 0x6b, 0x7c, 0xb6, 0xba, 0x45, 0x05 } };
};

class TextEntry4Gui : public PluginEditor
{
protected:
    In<std::string> pinValueIn;
    Out<std::string> pinValueOut;
    ObjectIn<IStyle> pinStyle;

    sdk::TextEditCallback callback;

public:
    TextEntry4Gui()
    {
        callback.onSuccess = [this](const std::string& text)
            {
                pinValueOut = text;
            };
    }

    ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
    {
        Graphics g(drawingContext);
        ClipDrawingToBounds _(g, bounds);

        // text colour from the Style's fill (default white); transparent background so the
        // editable readout sits over the artwork like the rendered text it replaces.
        gmpi::drawing::Color textColor = Colors::White;
        if (auto* style = pinStyle.value.get())
            style->getFillColor(&textColor);

        auto textRect = bounds;
        textRect.left += 2;
        textRect.top += 2;
        textRect.right -= 2;
        textRect.bottom -= 2;

        auto textFormat = g.getFactory().createTextFormat(getHeight(textRect));
        textFormat.setTextAlignment(gmpi::drawing::TextAlignment::Center);
        textFormat.setParagraphAlignment(gmpi::drawing::ParagraphAlignment::Center);

        auto textBrush = g.createSolidColorBrush(textColor);
        g.drawTextU(pinValueIn.value, textFormat, textRect, textBrush);
        return ReturnCode::Ok;
    }

    ReturnCode process() override
    {
        pinValueOut = pinValueIn.value;
        drawingHost->invalidateRect({});

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
		<Pin name="Style" datatype="object:style" />
	</GUI>
  </Plugin>
</PluginList>
)XML");
}

// A click-to-edit numeric readout. Folds the format + edit + parse into one module (no
// Float2Text / PatchMemUpdateFloatText plumbing): it shows the Value pin formatted to Decimal
// Places, with an optional non-editable Units suffix ("dB" -> "6.00 dB"). On commit it parses the
// text out the Value (out) pin; the value round-trips back through Value (in) - so feed Value (out)
// to a Value-Set and Value (in) from a Value (PatchMemGet).
//
// Editing is IN-PLACE via NumberEdit: the number is drawn with THIS control's own font + Style
// colour (no platform edit-box with a foreign font), and only the number is editable - the Units
// stay on screen, read-only, drawn just to the right so the two centre as one block.
class NumberEntry : public PluginEditor, public NumberEditClient
{
protected:
    In<float>        pinValueIn;
    In<std::string>  pinUnits;
    In<int32_t>      pinDecimalPlaces;
    ObjectIn<IStyle> pinStyle;
    ObjectIn<IStyle> pinUnitStyle;
    Out<float>       pinValueOut;
    In<bool>         pinTrigger; // rising edge starts keyboard entry (e.g. wired to MouseTarget's Double Click)

    NumberEdit numberEdit;
    bool editing = false;

public:
    NumberEntry() : numberEdit(*this)
    {
        pinDecimalPlaces.value = 2;
        // Open the in-place editor on a rising edge only (value==true). The host also pushes the pin's
        // initial value at load, which must NOT open the editor.
        pinTrigger.onUpdate = [this](PinBase*) { if (pinTrigger.value) startEditing(); };
    }

    void startEditing()
    {
        if (editing)
            return;

        // edit the NUMBER only (Units stay read-only on screen).
        numberEdit.setText(formatNumber(pinValueIn.value));
        editing = true;
        numberEdit.show(dialogHost, &bounds);
    }

    std::string formatNumber(float v) const
    {
        int dp = pinDecimalPlaces.value;
        if (dp < 0)
        {
            // "auto": fewer decimals as the magnitude grows, for a sensible readout (cf. displaydBD2
            // in SE2JUCE_parameterToString.h). -1 is the base: >=10 -> 0 dp, [0.1,10) -> 1 dp,
            // <0.1 -> 2 dp (one more, so small values keep precision). Each further -N adds one digit
            // per tier (so -2 gives 123 -> "123.0", 6.0 -> "6.00", 0.05 -> "0.050").
            const float a = std::fabs(v);
            const int extra = -dp - 1; // -1 -> +0, -2 -> +1, ...
            const int base = (a >= 10.0f) ? 0 : (a > 0.0f && a < 0.1f) ? 2 : 1;
            dp = base + extra;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "%.*f", dp, v);
        return buf;
    }

    ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
    {
        Graphics g(drawingContext);
        ClipDrawingToBounds _(g, bounds);

        // while not editing, the readout mirrors the parameter value (the editor owns the text
        // during a session). Cheap no-op when unchanged.
        if (!editing)
            numberEdit.setText(formatNumber(pinValueIn.value));

        // number colour from the Style's fill (default white); transparent background.
        gmpi::drawing::Color textColor = Colors::White;
        if (auto* style = pinStyle.value.get())
            style->getFillColor(&textColor);

        // units get their own (optional) Style; default to matching the number.
        gmpi::drawing::Color unitColor = textColor;
        if (auto* us = pinUnitStyle.value.get())
            us->getFillColor(&unitColor);

        // top-left text formats - NumberEdit does its own centring (and draws the cursor/selection).
        // The units are drawn ~2/3 the number's height (like the JUCE readout).
        const float numberHeight = getHeight(bounds);
        auto numberFormat = g.getFactory().createTextFormat(numberHeight);
        auto unitFormat   = g.getFactory().createTextFormat(numberHeight * (2.0f / 3.0f));

        // Reserve room for the read-only Units so the number + units centre together as one block.
        const std::string units = pinUnits.value.empty() ? std::string() : (" " + pinUnits.value);
        const float unitW = units.empty() ? 0.0f : unitFormat.getTextExtentU(units).width;

        Rect numberBounds = bounds;
        numberBounds.right -= unitW;
        numberEdit.render(g, numberFormat, numberBounds, textColor); // the editable number, in our style

        if (!units.empty())
        {
            const auto numSize = numberFormat.getTextExtentU(numberEdit.unsavedText());
            const float numLeft = 0.5f * (numberBounds.left + numberBounds.right - numSize.width);
            const float numTop  = 0.5f * (bounds.top + bounds.bottom - numSize.height);

            // Align the (smaller) units' baseline with the number's baseline.
            const float numBaseline = numTop + numberFormat.getFontMetrics().ascent;
            const float unitTop     = numBaseline - unitFormat.getFontMetrics().ascent;

            const Rect unitRect{ numLeft + numSize.width, unitTop, bounds.right, bounds.bottom };
            auto brush = g.createSolidColorBrush(unitColor);
            g.drawTextU(units, unitFormat, unitRect, brush);
        }

        return ReturnCode::Ok;
    }

    ReturnCode process() override
    {
        pinValueOut = pinValueIn.value;            // echo; the round-trip keeps the readout fresh

        if (!editing)
            numberEdit.setText(formatNumber(pinValueIn.value));

        if (drawingHost)
            drawingHost->invalidateRect({});

        return ReturnCode::Ok;
    }

    ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override
    {
        return ReturnCode::Ok;
    }
    gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override
    {
        if (editing)
        {
            // a click inside an active edit positions the caret (like a platform text editor).
            numberEdit.moveCursorToX(point.x);
            return ReturnCode::Handled;
        }

        inputHost->setCapture();
        return ReturnCode::Unhandled;
    }
    gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override
    {
        if (editing)
            return ReturnCode::Handled; // the press just repositioned the caret; don't restart editing

        inputHost->releaseCapture();
        startEditing();

        return ReturnCode::Unhandled;
    }

    // NumberEditClient - the in-place editor calls back here.
    void repaintText() override
    {
        if (drawingHost)
            drawingHost->invalidateRect({});
    }
    void setEditValue(std::string s) override
    {
        pinValueOut = static_cast<float>(strtod(s.c_str(), nullptr)); // emit; round-trips to Value (in)
    }
    void endEditValue() override
    {
        editing = false;
    }
};

namespace
{
auto r8C = gmpi::Register<NumberEntry>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: NumberEntry" name="Number Entry" category="GMPI/SDK Examples" vendor="Jeff McClintock">
	<GUI graphicsApi="GmpiGui">
		<Pin name="Value" datatype="float" />
		<Pin name="Units" datatype="string_utf8" />
		<Pin name="Decimal Places" datatype="int" default="2" />
		<Pin name="Style" datatype="object:style" />
		<Pin name="Units Style" datatype="object:style" />
		<Pin name="Value" datatype="float" direction="out" />
		<Pin name="Trigger" datatype="bool" />
	</GUI>
  </Plugin>
</PluginList>
)XML");
}

class MouseTarget : public PluginEditor
{
protected:
    Out<bool>  pinHover;             // 0
    Out<bool>  pinLClick;            // 1  left button
    Out<bool>  pinCClick;            // 2  centre (middle) button
    Out<bool>  pinRClick;            // 3  right button
    Out<bool>  pinDoubleClick;       // 4  true on the 2nd mouse-down, false on the 2nd mouse-up
    Out<float> pinX;                 // 5
    Out<float> pinY;                 // 6
    Out<float> pinDx;                // 7  movement delta since the last move (returns to zero next pass)
    Out<float> pinDy;                // 8

    // Movement deltas. We emit them from process() (not onPointerMove) so they behave exactly like
    // the Delta module: the delta is emitted in one process() pass and zeroed in the NEXT. Emitting
    // in the move handler and zeroing in process() would race - the pump's zeroing pass wipes the
    // delta before the downstream module's process() reads it.
    gmpi::drawing::Point curPoint{};  // latest position from onPointerMove
    gmpi::drawing::Point prevPoint{}; // position at the previous delta emission
    bool primed = false;              // seed prevPoint on the first move (no giant initial delta)
    bool wasDouble = false;

    static constexpr int32_t kFirst  = static_cast<int32_t>(gmpi::api::PointerFlags::FirstButton);
    static constexpr int32_t kSecond = static_cast<int32_t>(gmpi::api::PointerFlags::SecondButton);
    static constexpr int32_t kThird  = static_cast<int32_t>(gmpi::api::PointerFlags::ThirdButton);
    static constexpr int32_t kDouble = static_cast<int32_t>(gmpi::api::PointerFlags::Double);

public:
    gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t flags) override
    {
        inputHost->setCapture();
        curPoint = prevPoint = point; // deltas are measured from the press
        primed = true;

        // The flag identifies WHICH button; down vs up is the action (this handler = pressed).
        if (flags & kFirst)  pinLClick = true;
        if (flags & kSecond) pinRClick = true;
        if (flags & kThird)  pinCClick = true;

        // The OS sets Double on the second down; report it (cleared on the 2nd up below).
        wasDouble = flags & kDouble; // remember double-click on down.
        pinDoubleClick = false;

        return gmpi::ReturnCode::Ok;
    }

    gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t flags) override
    {
        pinX = point.x;
        pinY = point.y;

        if (!primed) { prevPoint = point; primed = true; }
        curPoint = point;
        editorHost2->setDirty(); // process() emits the delta

        return gmpi::ReturnCode::Ok;
    }

    gmpi::ReturnCode onPointerUp(gmpi::drawing::Point point, int32_t flags) override
    {
        inputHost->releaseCapture();

        if (flags & (kFirst | kSecond | kThird)) // host says which button released (e.g. Win32)
        {
            if (flags & kFirst)  pinLClick = false;
            if (flags & kSecond) pinRClick = false;
            if (flags & kThird)  pinCClick = false;
        }
        else // host didn't specify (e.g. the headless harness) - release all
        {
            pinLClick = false;
            pinRClick = false;
            pinCClick = false;
        }

        pinDoubleClick = wasDouble; // Set on the (2nd) up

        return gmpi::ReturnCode::Ok;
    }

    gmpi::ReturnCode process() override
    {
        // Emit the movement delta, then advance prevPoint so the NEXT pass emits zero (cf. Delta). The
        // delta must return to zero or a downstream integrator (value += dX) would run away.
        pinDx = curPoint.x - prevPoint.x;
        pinDy = curPoint.y - prevPoint.y;
        prevPoint = curPoint;
        if (pinDx.value != 0.0f || pinDy.value != 0.0f)
            editorHost2->setDirty(); // schedule one more pass to emit the zero
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
        <Pin name="Center Click" datatype="bool" direction="out"/>
        <Pin name="Right Click" datatype="bool" direction="out"/>
        <Pin name="Double Click" datatype="bool" direction="out"/>
        <Pin name="X" datatype="float" direction="out"/>
        <Pin name="Y" datatype="float" direction="out"/>
        <Pin name="dX" datatype="float" direction="out"/>
        <Pin name="dY" datatype="float" direction="out"/>
	</GUI>
  </Plugin>
</PluginList>
)XML");
}

class Delta final : public PluginEditorNoGui
{
    In<float> pinValue_in;
    Out<float> pinValue;

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
    In<float> pinValue_inA;
    In<float> pinValue_inB;
    Out<float> pinValue;

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
    In<float> pinValue_inA;
    In<float> pinValue_inB;
    Out<float> pinValue;

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
    In<bool> pinValue_in;
    Out<float> pinValue;

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
    In<float> pinValue_in;
    In<int> pinDecimals;
    Out<std::string> pinOutput;

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

        // Format using C++20 std::format.
        auto output = std::format("{:.{}f}", static_cast<double>(pinValue_in.value), decimals);

        // Replace -0.0 with 0.0 (same for -0.00 and -0.000 etc).
        // deliberate 'feature' of printf/format is to round small negative numbers to -0.0
        if (!output.empty() && output[0] == '-' && (float)pinValue_in.value > -1.0f)
        {
            int i = static_cast<int>(output.size()) - 1;
            while (i > 0)
            {
                if (output[static_cast<size_t>(i)] != '0' && output[static_cast<size_t>(i)] != '.')
                {
                    break;
                }
                --i;
            }
            if (i == 0) // nothing but zeros (or dot). remove leading minus sign.
            {
                output.erase(output.begin());
            }
        }

        pinOutput = output;

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
    In<std::string> pinValue_in;
    Out<std::wstring> pinOutput;

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
    In<std::wstring> pinValue_in;
    Out<std::string> pinOutput;

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
        gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
        unknown = phost; // asign assuming ownership is already managed from caller.

        drawingHost = unknown.as<gmpi::api::IDrawingHost>();

        gmpi::shared_ptr<gmpi::api::IUnknown> unknown2;
        drawingHost->getDrawingFactory(unknown2.put());
        unknown2->queryInterface(&drawing::api::IFactory::guid, AccessPtr::put_void(drawingFactory));

        return PluginEditorNoGui::setHost(phost);
    }
};

// a test interface and object to test passing ref-counted objects arround.
struct DECLSPEC_NOVTABLE ITest : gmpi::api::IUnknown
{
    virtual int32_t get() = 0;
    virtual void set(int32_t) = 0;

    // {BAC97FE8-3551-4A93-B362-AC3DBE296017}
    inline static const gmpi::api::Guid guid =
    { 0xbac97fe8, 0x3551, 0x4a93, { 0xb3, 0x62, 0xac, 0x3d, 0xbe, 0x29, 0x60, 0x17 } };
};

// an inplementation of the test interface.
struct TestObject : public ITest
{
    int32_t value{};

    int32_t get() override { return value; }
    void set(int32_t pvalue) override {value = pvalue;}

	TestObject()
	{
		_RPT0(0, "TestObject constructor\n");
	}
    ~TestObject()
	{
		_RPT0(0, "TestObject destructor\n");
	}

    GMPI_REFCOUNT
    GMPI_QUERYINTERFACE_METHOD(ITest)
};

// a test module that takes an input object, and returns a pointer to it's output object with the input incremented by 1.
struct ObjectTester final : public GraphicsProcessor
{
    ObjectIn<ITest> pinInput;
    ObjectOut<ITest> pinOutput;

    ReturnCode process() override
    {
        const bool firstTime = !pinOutput;
        if(firstTime)
            pinOutput.value.attach(new TestObject());

		const auto before = pinOutput.value->get();

        if(pinInput)
        {
            pinOutput.value->set(pinInput.value->get() + 1);

            _RPTN(0, "ObjectTester: %d -> %d\n", pinInput.value->get(), pinOutput.value->get());
        }
        else
        {
            _RPT0(0, "ObjectTester: no input\n");
			pinOutput.value->set(0);
        }

        if(firstTime || before != pinOutput.value->get())
        {
            pinOutput.send();
        }

        return ReturnCode::Ok;
    }
};

// register the test module
namespace
{
auto r17a = gmpi::Register<ObjectTester>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: ObjectTester" name="ObjectTester" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="In" datatype="object:test"/>
      <Pin name="Out" datatype="object:test" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}
struct CircleGeometry final : public GraphicsProcessor
{
    In<float> pinRadius;
    ObjectOut<drawing::api::IPathGeometry> pinOutput;

    gmpi::drawing::PathGeometry geometry;
    float builtRadius = -1.0f; // forces a build on the first process()

    ReturnCode process() override
    {
        // Build the circle at its ACTUAL radius (centred on the origin), so the size lives in the
        // geometry itself rather than in a render-time scale - which keeps stroke width in real
        // pixels. Rebuild only when the radius changes.
        if (!geometry || pinRadius.value != builtRadius)
        {
            geometry = drawingFactory.createPathGeometry();

            auto sink = geometry.open();

            const Point center{ 0.0f, 0.0f };
            const float radius = pinRadius.value;

            constexpr float pi = static_cast<float>(M_PI);
            sink.beginFigure({ center.x, center.y - radius }, drawing::FigureBegin::Filled);
			ArcSegment arc1{ { center.x, center.y + radius}, { radius, radius }, pi, SweepDirection::Clockwise, ArcSize::Small };
            ArcSegment arc2{ { center.x, center.y - radius}, { radius, radius }, pi, SweepDirection::Clockwise, ArcSize::Small };
            sink.addArc(arc1);
            sink.addArc(arc2);

            sink.endFigure(FigureEnd::Closed);
            sink.close();

            builtRadius = pinRadius.value;
            pinOutput = AccessPtr::get(geometry);
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
      <Pin name="Radius" datatype="float" default="10"/>
      <Pin name="Path" datatype="object:path" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// A pure open-arc primitive (centred on the origin): an arc of the given Radius from Start,
// sweeping Sweep turns. Built at its real radius so stroke width stays in pixels. To make a
// value-arc, drive Sweep from a normalized value scaled by a Multiply - the mapping lives
// OUTSIDE this module. Angles are in TURNS; +ve sweep is clockwise (screen coords, y down).
struct ArcGeometry final : public GraphicsProcessor
{
    In<float> pinRadius;
    In<float> pinStartTurns;
    In<float> pinSweepTurns;
    ObjectOut<drawing::api::IPathGeometry> pinPath;

    gmpi::drawing::PathGeometry geometry;
    float bR = -1e9f, bS = -1e9f, bW = -1e9f;

    ReturnCode process() override
    {
        if (geometry && pinRadius.value == bR && pinStartTurns.value == bS && pinSweepTurns.value == bW)
            return ReturnCode::Ok; // nothing relevant changed

        bR = pinRadius.value; bS = pinStartTurns.value; bW = pinSweepTurns.value;

        geometry = drawingFactory.createPathGeometry();
        auto sink = geometry.open();

        const float tau = 2.0f * static_cast<float>(M_PI);
        const float R = pinRadius.value;
        // clamp away from a full turn to avoid a degenerate start==end arc (ambiguous between
        // zero and a full circle).
        const float sweep = std::clamp(pinSweepTurns.value, -0.999f, 0.999f);
        if (std::fabs(sweep) > 1e-4f)
        {
            const float a0 = pinStartTurns.value * tau;
            const float a1 = (pinStartTurns.value + sweep) * tau;
            sink.beginFigure({ R * cosf(a0), R * sinf(a0) }, drawing::FigureBegin::Hollow);
            ArcSegment arc{ { R * cosf(a1), R * sinf(a1) }, { R, R }, 0.0f,
                sweep >= 0 ? SweepDirection::Clockwise : SweepDirection::CounterClockwise,
                std::fabs(sweep) > 0.5f ? ArcSize::Large : ArcSize::Small };
            sink.addArc(arc);
            sink.endFigure(FigureEnd::Open);
        }

        sink.close();
        pinPath = AccessPtr::get(geometry);
        return ReturnCode::Ok;
    }
};

namespace
{
auto r41 = gmpi::Register<ArcGeometry>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: ArcGeometry" name="Arc Geometry" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Radius" datatype="float" default="10"/>
      <Pin name="Start (turns)" datatype="float" default="0.3"/>
      <Pin name="Sweep (turns)" datatype="float" default="0.75"/>
      <Pin name="Path" datatype="object:path" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct SvgGeometry final : public GraphicsProcessor
{
    In<std::string> pinFilename;
    ObjectOut<drawing::api::IPathGeometry> pinOutput;

    gmpi::drawing::PathGeometry geometry;

    ReturnCode process() override
    {
        if (!pinOutput)
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

            pinOutput = AccessPtr::get(geometry);
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
      <Pin name="Path" datatype="object:path" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// Build a gmpi::drawing::Color from RGBA floats and pass it out a 'struct:color' pin.
// The colour travels as a plain value (the Color struct itself), not a reference-counted interface.
struct ColorFromRGBA final : public PluginEditorNoGui
{
    In<float> pinRed;
    In<float> pinGreen;
    In<float> pinBlue;
    In<float> pinAlpha;
    Out<gmpi::drawing::Color> pinColor;

    ReturnCode process() override
    {
        pinColor = gmpi::drawing::Color{ pinRed.value, pinGreen.value, pinBlue.value, pinAlpha.value };
        return ReturnCode::Ok;
    }
};

namespace
{
auto r25 = gmpi::Register<ColorFromRGBA>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: ColorFromRGBA" name="Color (RGBA)" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Red" datatype="float"/>
      <Pin name="Green" datatype="float"/>
      <Pin name="Blue" datatype="float"/>
      <Pin name="Alpha" datatype="float" default="1"/>
      <Pin name="Color" datatype="struct:color" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// ---- Transform producers ----------------------------------------------------------------
// A transform is a 'struct:transform' = gmpi::drawing::Matrix3x2 (a 2D affine matrix, plain
// value like a Color). Matrix3x2 defaults to identity, so an unconnected transform pin is a
// harmless no-op. These are atomic (one transform each); chain them with Combine Transforms.

// Rotation by an angle given in TURNS (1 turn = a full circle), not radians.
struct Rotation final : public PluginEditorNoGui
{
    In<float>                    pinTurns;
    Out<gmpi::drawing::Matrix3x2> pinTransform;

    ReturnCode process() override
    {
        const float radians = pinTurns.value * 2.0f * static_cast<float>(M_PI);
        pinTransform = makeRotation(radians);
        return ReturnCode::Ok;
    }
};

namespace
{
auto r27 = gmpi::Register<Rotation>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: Rotation" name="Rotation" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Angle (turns)" datatype="float"/>
      <Pin name="Transform" datatype="struct:transform" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// Translation by (X, Y).
struct Translation final : public PluginEditorNoGui
{
    In<float>                    pinX;
    In<float>                    pinY;
    Out<gmpi::drawing::Matrix3x2> pinTransform;

    ReturnCode process() override
    {
        pinTransform = makeTranslation(pinX.value, pinY.value);
        return ReturnCode::Ok;
    }
};

namespace
{
auto r28 = gmpi::Register<Translation>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: Translation" name="Translation" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="X" datatype="float"/>
      <Pin name="Y" datatype="float"/>
      <Pin name="Transform" datatype="struct:transform" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// Scale by (X, Y). Defaults to 1 (identity) so an unconnected scale doesn't collapse geometry.
struct Scale final : public PluginEditorNoGui
{
    In<float>                    pinX;
    In<float>                    pinY;
    Out<gmpi::drawing::Matrix3x2> pinTransform;

    ReturnCode process() override
    {
        pinTransform = makeScale(pinX.value, pinY.value);
        return ReturnCode::Ok;
    }
};

namespace
{
auto r29 = gmpi::Register<Scale>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: Scale" name="Scale" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="X" datatype="float" default="1"/>
      <Pin name="Y" datatype="float" default="1"/>
      <Pin name="Transform" datatype="struct:transform" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// Combine two transforms into one: Result = A * B (i.e. apply A, then B).
struct CombineTransforms final : public PluginEditorNoGui
{
    In<gmpi::drawing::Matrix3x2> pinA;
    In<gmpi::drawing::Matrix3x2> pinB;
    Out<gmpi::drawing::Matrix3x2> pinResult;

    ReturnCode process() override
    {
        pinResult = pinA.value * pinB.value;
        return ReturnCode::Ok;
    }
};

namespace
{
auto r30 = gmpi::Register<CombineTransforms>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: CombineTransforms" name="Combine Transforms" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="A" datatype="struct:transform"/>
      <Pin name="B" datatype="struct:transform"/>
      <Pin name="A x B" datatype="struct:transform" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// Concrete, mutable style. Reference counted; the producing module owns it.
struct StyleObject final : public IStyle
{
    gmpi::drawing::Color fillColor{};                 // alpha 0 = no fill
    gmpi::drawing::Color strokeColor{ 0, 0, 0, 1 };   // opaque black
    float strokeWidth = 1.0f;
    int32_t strokeCap = 0;                            // CapStyle::Flat

    gmpi::ReturnCode getFillColor(gmpi::drawing::Color* c) override { *c = fillColor; return gmpi::ReturnCode::Ok; }
    gmpi::ReturnCode getStrokeColor(gmpi::drawing::Color* c) override { *c = strokeColor; return gmpi::ReturnCode::Ok; }
    gmpi::ReturnCode getStrokeWidth(float* w) override { *w = strokeWidth; return gmpi::ReturnCode::Ok; }
    int32_t getStrokeCap() override { return strokeCap; }

    GMPI_QUERYINTERFACE_METHOD(IStyle);
    GMPI_REFCOUNT
};

// Bundles fill colour, stroke colour and stroke width into a single 'object:style' output.
// Build the style once and fan the one wire out to as many renderers as you like.
struct StyleBuilder final : public PluginEditorNoGui
{
    In<gmpi::drawing::Color> pinFill;
    In<gmpi::drawing::Color> pinStroke;
    In<float>                pinStrokeWidth;
    In<int32_t>              pinCap;
    ObjectOut<IStyle>         pinStyle;

    StyleBuilder()
    {
        // visible defaults: opaque-black 1px stroke, no fill (fill alpha stays 0).
        pinStroke.value = gmpi::drawing::Color{ 0, 0, 0, 1 };
        pinStrokeWidth.value = 1.0f;
    }

    ReturnCode process() override
    {
        // create the style object once, then keep mutating it in place and re-sending.
        if (!pinStyle)
            pinStyle.value.attach(new StyleObject());

        auto* s = static_cast<StyleObject*>(pinStyle.value.get());
        s->fillColor   = pinFill.value;
        s->strokeColor = pinStroke.value;
        s->strokeWidth = pinStrokeWidth.value;
        s->strokeCap   = pinCap.value;

        pinStyle.send();
        return ReturnCode::Ok;
    }
};

namespace
{
auto r26 = gmpi::Register<StyleBuilder>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: StyleBuilder" name="Style" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Fill" datatype="struct:color"/>
      <Pin name="Stroke" datatype="struct:color"/>
      <Pin name="Stroke Width" datatype="float" default="1"/>
      <Pin name="Cap" datatype="enum" default="0" metadata="Flat,Square,Round"/>
      <Pin name="Style" datatype="object:style" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct RenderGeometry final : public PluginEditor
{
    ObjectIn<drawing::api::IPathGeometry> pinInput;
    ObjectIn<IStyle>                      pinStyle;

    RenderGeometry()
    {
        // redraw whenever the geometry or its style changes.
        auto invalidate = [this](PinBase*) { if (drawingHost) drawingHost->invalidateRect({}); };
        pinInput.onUpdate = invalidate;
        pinStyle.onUpdate = invalidate;
    }

    ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
    {
		if (!pinInput)
			return ReturnCode::Ok;

        Graphics g(drawingContext);
        ClipDrawingToBounds _(g, bounds);

        // wrap the incoming interface so we can draw it.
        PathGeometry geometry;
        if (ReturnCode::Ok != pinInput.value->queryInterface(&drawing::api::IPathGeometry::guid, AccessPtr::put_void(geometry)))
            return ReturnCode::Fail;

        // resolve the style, falling back to a built-in default (white 1px stroke, no fill)
        // when nothing is connected.
        gmpi::drawing::Color fill{};                  // alpha 0 = no fill
        gmpi::drawing::Color stroke = Colors::White;
        float strokeWidth = 1.0f;
        int32_t cap = 0;
        if (auto* style = pinStyle.value.get())
        {
            style->getFillColor(&fill);
            style->getStrokeColor(&stroke);
            style->getStrokeWidth(&strokeWidth);
            cap = style->getStrokeCap();
        }


        // origin at the widget centre (geometry is authored centred on origin).
        TempTransform tt(g, makeTranslation(getWidth(bounds) * 0.5f, getHeight(bounds) * 0.5f));

        // fill the interior, then stroke the outline (alpha 0 / zero width = skip).
        if (fill.a > 0.0f)
        {
            auto fillBrush = g.createSolidColorBrush(fill);
            g.fillGeometry(geometry, fillBrush);
        }
        if (stroke.a > 0.0f && strokeWidth > 0.0f)
        {
            auto strokeBrush = g.createSolidColorBrush(stroke);
            auto strokeStyle = g.getFactory().createStrokeStyle(static_cast<CapStyle>(cap));
            g.drawGeometry(geometry, strokeBrush, strokeWidth, strokeStyle);
        }

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
      <Pin name="Path" datatype="object:path"/>
      <Pin name="Style" datatype="object:style"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// ============================================================================================
// Instancing proof-of-concept: one template geometry drawn N times under a LIST of transforms.
// Mirrors Houdini Copy-to-Points / Unreal ISM / TouchDesigner Geometry COMP: repetition lives
// in the DATA (a transform list on one wire), not in duplicated nodes/wires.
// ============================================================================================

// A list of transforms carried on one 'object:transformlist' wire. Reference counted.
struct DECLSPEC_NOVTABLE ITransformList : gmpi::api::IUnknown
{
    virtual int32_t getCount() = 0;
    virtual gmpi::ReturnCode getTransform(int32_t index, gmpi::drawing::Matrix3x2* returnTransform) = 0;

    // {55CECBC5-88D6-48CB-83A4-DD42814BD8D3}
    inline static const gmpi::api::Guid guid =
    { 0x55cecbc5, 0x88d6, 0x48cb, { 0x83, 0xa4, 0xdd, 0x42, 0x81, 0x4b, 0xd8, 0xd3 } };
};

struct TransformListObject final : public ITransformList
{
    std::vector<gmpi::drawing::Matrix3x2> transforms;

    int32_t getCount() override { return static_cast<int32_t>(transforms.size()); }
    gmpi::ReturnCode getTransform(int32_t index, gmpi::drawing::Matrix3x2* out) override
    {
        if (index < 0 || index >= static_cast<int32_t>(transforms.size()))
            return gmpi::ReturnCode::Fail;
        *out = transforms[index];
        return gmpi::ReturnCode::Ok;
    }

    GMPI_QUERYINTERFACE_METHOD(ITransformList);
    GMPI_REFCOUNT
};

// Generates N translation transforms evenly spaced around a ring - the "spawn 100 points"
// half: one module emits the whole collection on a single wire.
struct RingOfTransforms final : public PluginEditorNoGui
{
    In<int32_t> pinCount;
    In<float>   pinRadius;
    ObjectOut<ITransformList> pinTransforms;

    ReturnCode process() override
    {
        if (!pinTransforms)
            pinTransforms.value.attach(new TransformListObject());

        // Lay the ring out around the origin (0,0); the renderer centres its own frame, so we
        // don't need to know the target widget's size or position. This generator only places
        // points - compose a Scale/Rotate onto the whole list downstream with Transform Each.
        auto* list = static_cast<TransformListObject*>(pinTransforms.value.get());
        const int n = (std::max)(0, pinCount.value);
        list->transforms.resize(n);
        for (int i = 0; i < n; ++i)
        {
            const float angle = static_cast<float>(i) / static_cast<float>((std::max)(1, n)) * 2.0f * static_cast<float>(M_PI);
            list->transforms[i] = makeTranslation(pinRadius.value * cosf(angle), pinRadius.value * sinf(angle));
        }

        pinTransforms.send();
        return ReturnCode::Ok;
    }
};

namespace
{
auto r31 = gmpi::Register<RingOfTransforms>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: RingOfTransforms" name="Ring of Transforms" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Count" datatype="int" default="12"/>
      <Pin name="Radius" datatype="float" default="40"/>
      <Pin name="Transforms" datatype="object:transformlist" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// ============================================================================================
// Generic layout spine. RingOfTransforms is a convenience preset; the same ring (or a line,
// spiral, ...) can be built from these primitives, with both repetition AND per-element variation
// living in the data (cf. Houdini point attributes / Blender fields). A 'number list' carries one
// scalar per element; per-element math maps a function over the whole list; an assembler turns
// coordinate lists into transforms. Angles are in TURNS (1 turn = full circle).
// ============================================================================================

// A list of scalars carried on one 'object:numberlist' wire (a per-element attribute).
struct DECLSPEC_NOVTABLE INumberList : gmpi::api::IUnknown
{
    virtual int32_t getCount() = 0;
    virtual gmpi::ReturnCode getValue(int32_t index, float* returnValue) = 0;

    // {EDA1B70A-9592-4B8E-B712-B3CD49561F6B}
    inline static const gmpi::api::Guid guid =
    { 0xeda1b70a, 0x9592, 0x4b8e, { 0xb7, 0x12, 0xb3, 0xcd, 0x49, 0x56, 0x1f, 0x6b } };
};

struct NumberListObject final : public INumberList
{
    std::vector<float> values;

    int32_t getCount() override { return static_cast<int32_t>(values.size()); }
    gmpi::ReturnCode getValue(int32_t index, float* out) override
    {
        if (index < 0 || index >= static_cast<int32_t>(values.size()))
            return gmpi::ReturnCode::Fail;
        *out = values[index];
        return gmpi::ReturnCode::Ok;
    }

    GMPI_QUERYINTERFACE_METHOD(INumberList);
    GMPI_REFCOUNT
};

// Spawn N numbers spread from Start to End (End exclusive, so a full-turn ring closes cleanly):
//   value[i] = Start + (End - Start) * i / Count.   This is the "make N elements" primitive.
struct Series final : public PluginEditorNoGui
{
    In<int32_t> pinCount;
    In<float>   pinStart;
    In<float>   pinEnd;
    ObjectOut<INumberList> pinOut;

    ReturnCode process() override
    {
        if (!pinOut)
            pinOut.value.attach(new NumberListObject());

        auto* out = static_cast<NumberListObject*>(pinOut.value.get());
        const int n = (std::max)(0, pinCount.value);
        out->values.resize(n);
        for (int i = 0; i < n; ++i)
            out->values[i] = pinStart.value + (pinEnd.value - pinStart.value) * (static_cast<float>(i) / static_cast<float>((std::max)(1, n)));

        pinOut.send();
        return ReturnCode::Ok;
    }
};

namespace
{
auto r34 = gmpi::Register<Series>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: Series" name="Series" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Count" datatype="int" default="12"/>
      <Pin name="Start" datatype="float" default="0"/>
      <Pin name="End" datatype="float" default="1"/>
      <Pin name="Numbers" datatype="object:numberlist" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// Per-element math: maps one function over every value in a number list (cf. Houdini wrangle /
// Blender field). K is the operand for Multiply/Add; Cos/Sin take their input in TURNS.
struct NumberMath final : public PluginEditorNoGui
{
    In<int32_t> pinOp;        // 0=Multiply, 1=Add, 2=Cos, 3=Sin
    In<float>   pinK;
    ObjectIn<INumberList>  pinIn;
    ObjectOut<INumberList> pinOut;

    ReturnCode process() override
    {
        if (!pinOut)
            pinOut.value.attach(new NumberListObject());

        auto* out = static_cast<NumberListObject*>(pinOut.value.get());
        if (auto* in = pinIn.value.get())
        {
            const int n = in->getCount();
            const float k = pinK.value;
            const float turns = 2.0f * static_cast<float>(M_PI);
            out->values.resize(n);
            for (int i = 0; i < n; ++i)
            {
                float v{};
                in->getValue(i, &v);
                switch (pinOp.value)
                {
                default:
                case 0: v = v * k;            break;
                case 1: v = v + k;            break;
                case 2: v = cosf(v * turns);  break;
                case 3: v = sinf(v * turns);  break;
                }
                out->values[i] = v;
            }
        }
        else
        {
            out->values.clear();
        }

        pinOut.send();
        return ReturnCode::Ok;
    }
};

namespace
{
auto r35 = gmpi::Register<NumberMath>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: NumberMath" name="Number Math" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Op" datatype="enum" default="0" metadata="Multiply,Add,Cos (turns),Sin (turns)"/>
      <Pin name="K" datatype="float" default="1"/>
      <Pin name="Numbers" datatype="object:numberlist"/>
      <Pin name="Numbers" datatype="object:numberlist" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// Assemble a transform list from coordinate lists: transform[i] = translate(X[i], Y[i]).
// A missing/short list contributes 0 for that axis (so X-only gives a line along X).
struct TranslateXY final : public PluginEditorNoGui
{
    ObjectIn<INumberList>     pinX;
    ObjectIn<INumberList>     pinY;
    ObjectOut<ITransformList> pinOut;

    ReturnCode process() override
    {
        if (!pinOut)
            pinOut.value.attach(new TransformListObject());

        auto* out = static_cast<TransformListObject*>(pinOut.value.get());
        auto* xs = pinX.value.get();
        auto* ys = pinY.value.get();
        const int nx = xs ? xs->getCount() : 0;
        const int ny = ys ? ys->getCount() : 0;
        const int n = (std::max)(nx, ny);
        out->transforms.resize(n);
        for (int i = 0; i < n; ++i)
        {
            float x{}, y{};
            if (i < nx) xs->getValue(i, &x);
            if (i < ny) ys->getValue(i, &y);
            out->transforms[i] = makeTranslation(x, y);
        }

        pinOut.send();
        return ReturnCode::Ok;
    }
};

namespace
{
auto r36 = gmpi::Register<TranslateXY>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: TranslateXY" name="Translate XY" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="X" datatype="object:numberlist"/>
      <Pin name="Y" datatype="object:numberlist"/>
      <Pin name="Transforms" datatype="object:transformlist" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// Composes one transform onto every element of a transform list - the dataflow "map over the
// collection" operation (cf. a Grasshopper component / Houdini wrangle). out[i] = Transform * In[i],
// i.e. Transform is applied to the template first, then each item's own placement. This is how you
// scale a unit template, rotate/offset the whole field, etc. WITHOUT baking it into the generator.
// Transform defaults to identity, so an unconnected node passes the list through unchanged.
struct TransformEach final : public PluginEditorNoGui
{
    In<gmpi::drawing::Matrix3x2> pinTransform;
    ObjectIn<ITransformList>      pinIn;
    ObjectOut<ITransformList>     pinOut;

    ReturnCode process() override
    {
        if (!pinOut)
            pinOut.value.attach(new TransformListObject());

        auto* out = static_cast<TransformListObject*>(pinOut.value.get());
        if (auto* in = pinIn.value.get())
        {
            const int n = in->getCount();
            out->transforms.resize(n);
            for (int i = 0; i < n; ++i)
            {
                gmpi::drawing::Matrix3x2 t;
                in->getTransform(i, &t);
                out->transforms[i] = pinTransform.value * t;
            }
        }
        else
        {
            out->transforms.clear();
        }

        pinOut.send();
        return ReturnCode::Ok;
    }
};

namespace
{
auto r33 = gmpi::Register<TransformEach>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: TransformEach" name="Transform Each" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Transform" datatype="struct:transform"/>
      <Pin name="Transforms" datatype="object:transformlist"/>
      <Pin name="Transforms" datatype="object:transformlist" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// ---- Per-element (per-instance) variation ---------------------------------------------------
// Assemble per-instance SCALE transforms from number lists, then compose them element-wise onto
// the placement list. This is how a number list drives a different size for every instance (cf.
// Houdini's pscale attribute / Blender capturing a field). Y unconnected = uniform scale.
struct ScaleXY final : public PluginEditorNoGui
{
    ObjectIn<INumberList>     pinX;
    ObjectIn<INumberList>     pinY;
    ObjectOut<ITransformList> pinOut;

    ReturnCode process() override
    {
        if (!pinOut)
            pinOut.value.attach(new TransformListObject());

        auto* out = static_cast<TransformListObject*>(pinOut.value.get());
        auto* xs = pinX.value.get();
        auto* ys = pinY.value.get();
        const int nx = xs ? xs->getCount() : 0;
        const int ny = ys ? ys->getCount() : 0;
        const int n = (std::max)(nx, ny);
        out->transforms.resize(n);
        for (int i = 0; i < n; ++i)
        {
            float x = 1.0f, y = 1.0f;
            if (i < nx) xs->getValue(i, &x);
            if (ny > 0) { if (i < ny) ys->getValue(i, &y); }  // Y connected: use it
            else        { y = x; }                            // Y unconnected: uniform scale
            out->transforms[i] = makeScale(x, y);
        }

        pinOut.send();
        return ReturnCode::Ok;
    }
};

namespace
{
auto r37 = gmpi::Register<ScaleXY>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: ScaleXY" name="Scale XY" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="X" datatype="object:numberlist"/>
      <Pin name="Y" datatype="object:numberlist"/>
      <Pin name="Transforms" datatype="object:transformlist" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// Compose two transform lists element-wise: out[i] = A[i] * B[i] (apply A then B). The list
// version of CombineTransforms - e.g. CombineEach(per-instance scales, placements) gives each
// instance its own size at its own position. Counts that differ are truncated to the shorter.
struct CombineEach final : public PluginEditorNoGui
{
    ObjectIn<ITransformList>  pinA;
    ObjectIn<ITransformList>  pinB;
    ObjectOut<ITransformList> pinOut;

    ReturnCode process() override
    {
        if (!pinOut)
            pinOut.value.attach(new TransformListObject());

        auto* out = static_cast<TransformListObject*>(pinOut.value.get());
        auto* a = pinA.value.get();
        auto* b = pinB.value.get();
        const int na = a ? a->getCount() : 0;
        const int nb = b ? b->getCount() : 0;
        const int n = (std::min)(na, nb);
        out->transforms.resize(n);
        for (int i = 0; i < n; ++i)
        {
            gmpi::drawing::Matrix3x2 ta, tb;
            a->getTransform(i, &ta);
            b->getTransform(i, &tb);
            out->transforms[i] = ta * tb;
        }

        pinOut.send();
        return ReturnCode::Ok;
    }
};

namespace
{
auto r38 = gmpi::Register<CombineEach>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: CombineEach" name="Combine Each" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="A" datatype="object:transformlist"/>
      <Pin name="B" datatype="object:transformlist"/>
      <Pin name="A x B" datatype="object:transformlist" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// Draws one template geometry once per transform in the list - the instancing renderer.
// Author the look once (template + style); the transform list drives how many copies appear.
struct RenderInstances final : public PluginEditor
{
    ObjectIn<drawing::api::IPathGeometry> pinTemplate;
    ObjectIn<ITransformList>              pinTransforms;
    ObjectIn<IStyle>                      pinStyle;

    RenderInstances()
    {
        auto invalidate = [this](PinBase*) { if (drawingHost) drawingHost->invalidateRect({}); };
        pinTemplate.onUpdate = invalidate;
        pinTransforms.onUpdate = invalidate;
        pinStyle.onUpdate = invalidate;
    }

    ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
    {
        if (!pinTemplate || !pinTransforms)
            return ReturnCode::Ok;

        Graphics g(drawingContext);
        ClipDrawingToBounds _(g, bounds);

        // wrap the template geometry.
        PathGeometry geometry;
        if (ReturnCode::Ok != pinTemplate.value->queryInterface(&drawing::api::IPathGeometry::guid, AccessPtr::put_void(geometry)))
            return ReturnCode::Fail;

        // resolve the style (default white 1px stroke, no fill).
        gmpi::drawing::Color fill{};
        gmpi::drawing::Color stroke = Colors::White;
        float strokeWidth = 1.0f;
        int32_t cap = 0;
        if (auto* style = pinStyle.value.get())
        {
            style->getFillColor(&fill);
            style->getStrokeColor(&stroke);
            style->getStrokeWidth(&strokeWidth);
            cap = style->getStrokeCap();
        }

        // brushes/stroke-style are transform-independent, so create once and reuse for every instance.
        auto fillBrush = g.createSolidColorBrush(fill);
        auto strokeBrush = g.createSolidColorBrush(stroke);
        auto strokeStyle = g.getFactory().createStrokeStyle(static_cast<CapStyle>(cap));

        auto* list = pinTransforms.value.get();

        // Put the origin at the widget's centre, so upstream generators can lay instances out
        // around (0,0) without knowing the widget's size or position.
        const auto originalTransform = g.getTransform();
        const auto base = makeTranslation(getWidth(bounds) * 0.5f, getHeight(bounds) * 0.5f) * originalTransform;

        const int count = list->getCount();
        for (int i = 0; i < count; ++i)
        {
            gmpi::drawing::Matrix3x2 t;
            if (ReturnCode::Ok != list->getTransform(i, &t))
                continue;

            g.setTransform(t * base);  // place this instance, relative to the widget centre.

            if (fill.a > 0.0f)
                g.fillGeometry(geometry, fillBrush);
            if (stroke.a > 0.0f && strokeWidth > 0.0f)
                g.drawGeometry(geometry, strokeBrush, strokeWidth, strokeStyle);
        }
        g.setTransform(originalTransform);

        return ReturnCode::Ok;
    }
};

namespace
{
auto r32 = gmpi::Register<RenderInstances>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: RenderInstances" name="Render Instances" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Path" datatype="object:path"/>
      <Pin name="Transforms" datatype="object:transformlist"/>
      <Pin name="Style" datatype="object:style"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// ============================================================================================
// Per-instance GEOMETRY: a list of paths, one per instance. This is how you vary RADIUS with a
// CONSTANT stroke width - each circle is built at its own real radius (size in the geometry, not
// a render scale) and drawn at 1:1, so the stroke isn't scaled. (cf. Houdini copying a different
// piece per point.) Contrast RenderInstances, where one template is scaled per instance and the
// stroke scales with it.
// ============================================================================================

// A list of path geometries carried on one 'object:pathlist' wire.
struct DECLSPEC_NOVTABLE IPathList : gmpi::api::IUnknown
{
    virtual int32_t getCount() = 0;
    // Returns a BORROWED pointer (the list owns it; valid while you hold the list).
    virtual gmpi::ReturnCode getPath(int32_t index, drawing::api::IPathGeometry** returnPath) = 0;

    // {80415BF2-B842-480A-ADB4-33D6CCA40DD3}
    inline static const gmpi::api::Guid guid =
    { 0x80415bf2, 0xb842, 0x480a, { 0xad, 0xb4, 0x33, 0xd6, 0xcc, 0xa4, 0x0d, 0xd3 } };
};

struct PathListObject final : public IPathList
{
    std::vector<gmpi::drawing::PathGeometry> paths;

    int32_t getCount() override { return static_cast<int32_t>(paths.size()); }
    gmpi::ReturnCode getPath(int32_t index, drawing::api::IPathGeometry** out) override
    {
        if (index < 0 || index >= static_cast<int32_t>(paths.size()))
        {
            *out = nullptr;
            return gmpi::ReturnCode::Fail;
        }
        *out = AccessPtr::get(paths[index]); // borrowed
        return gmpi::ReturnCode::Ok;
    }

    GMPI_QUERYINTERFACE_METHOD(IPathList);
    GMPI_REFCOUNT
};

// Build one circle geometry per radius in the list, each at its ACTUAL radius (centred on the
// origin). Pair with placement-only transforms + RenderEach for constant stroke width.
struct Circles final : public GraphicsProcessor
{
    ObjectIn<INumberList> pinRadii;
    ObjectOut<IPathList>  pinPaths;

    ReturnCode process() override
    {
        if (!pinPaths)
            pinPaths.value.attach(new PathListObject());

        auto* out = static_cast<PathListObject*>(pinPaths.value.get());
        out->paths.clear();

        if (auto* radii = pinRadii.value.get())
        {
            const int n = radii->getCount();
            out->paths.reserve(n);
            constexpr float pi = static_cast<float>(M_PI);
            for (int i = 0; i < n; ++i)
            {
                float radius = 1.0f;
                radii->getValue(i, &radius);

                auto geom = drawingFactory.createPathGeometry();
                auto sink = geom.open();
                sink.beginFigure({ 0.0f, -radius }, drawing::FigureBegin::Filled);
                sink.addArc({ { 0.0f,  radius }, { radius, radius }, pi, SweepDirection::Clockwise, ArcSize::Small });
                sink.addArc({ { 0.0f, -radius }, { radius, radius }, pi, SweepDirection::Clockwise, ArcSize::Small });
                sink.endFigure(FigureEnd::Closed);
                sink.close();

                out->paths.push_back(std::move(geom));
            }
        }

        pinPaths.send();
        return ReturnCode::Ok;
    }
};

namespace
{
auto r39 = gmpi::Register<Circles>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: Circles" name="Circles" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Radius" datatype="object:numberlist"/>
      <Pin name="Paths" datatype="object:pathlist" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// Draws a LIST of geometries, one per transform (zipped): path[i] at transform[i]. Because each
// geometry already carries its real size and the transform is placement-only, the stroke width is
// drawn at 1:1 - the same in pixels for every instance regardless of its radius.
struct RenderEach final : public PluginEditor
{
    ObjectIn<IPathList>      pinPaths;
    ObjectIn<ITransformList> pinTransforms;
    ObjectIn<IStyle>         pinStyle;

    RenderEach()
    {
        auto invalidate = [this](PinBase*) { if (drawingHost) drawingHost->invalidateRect({}); };
        pinPaths.onUpdate = invalidate;
        pinTransforms.onUpdate = invalidate;
        pinStyle.onUpdate = invalidate;
    }

    ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
    {
        if (!pinPaths || !pinTransforms)
            return ReturnCode::Ok;

        Graphics g(drawingContext);
        ClipDrawingToBounds _(g, bounds);

        auto* paths = pinPaths.value.get();
        auto* xforms = pinTransforms.value.get();

        gmpi::drawing::Color fill{};
        gmpi::drawing::Color stroke = Colors::White;
        float strokeWidth = 1.0f;
        int32_t cap = 0;
        if (auto* style = pinStyle.value.get())
        {
            style->getFillColor(&fill);
            style->getStrokeColor(&stroke);
            style->getStrokeWidth(&strokeWidth);
            cap = style->getStrokeCap();
        }


        auto fillBrush = g.createSolidColorBrush(fill);
        auto strokeBrush = g.createSolidColorBrush(stroke);
        auto strokeStyle = g.getFactory().createStrokeStyle(static_cast<CapStyle>(cap));

		const auto originalTransform = g.getTransform();
        const auto base = makeTranslation(getWidth(bounds) * 0.5f, getHeight(bounds) * 0.5f) * originalTransform;
        const int n = (std::min)(paths->getCount(), xforms->getCount());
        for (int i = 0; i < n; ++i)
        {
            drawing::api::IPathGeometry* raw{};
            if (ReturnCode::Ok != paths->getPath(i, &raw) || !raw)
                continue;

            PathGeometry geometry;
            raw->queryInterface(&drawing::api::IPathGeometry::guid, AccessPtr::put_void(geometry));

            gmpi::drawing::Matrix3x2 t;
            xforms->getTransform(i, &t);
            g.setTransform(t * base); // placement only -> stroke width stays at 1:1 (constant)

            if (fill.a > 0.0f)
                g.fillGeometry(geometry, fillBrush);
            if (stroke.a > 0.0f && strokeWidth > 0.0f)
                g.drawGeometry(geometry, strokeBrush, strokeWidth, strokeStyle);
        }
        g.setTransform(originalTransform);

        return ReturnCode::Ok;
    }
};

namespace
{
auto r40 = gmpi::Register<RenderEach>::withXml(R"XML(
<?xml version="1.0" encoding="utf-8" ?>

<PluginList>
  <Plugin id="SE: RenderEach" name="Render Each" category="GMPI/SDK Examples" vendor="Jeff McClintock">
    <GUI>
      <Pin name="Paths" datatype="object:pathlist"/>
      <Pin name="Transforms" datatype="object:transformlist"/>
      <Pin name="Style" datatype="object:style"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct Render2Bitmap final : public GraphicsProcessor
{
    ObjectIn<drawing::api::IPathGeometry>  pinInput;
    ObjectOut<drawing::api::IBitmap>       pinOutput;

    Bitmap bitmap;

    ReturnCode process() override
    {
        if (!pinInput)
            return ReturnCode::Ok;

        // wrap the incoming interface so we can draw it.
        PathGeometry geometry;
        if (ReturnCode::Ok != pinInput.value->queryInterface(&drawing::api::IPathGeometry::guid, AccessPtr::put_void(geometry)))
            return ReturnCode::Fail;
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

        pinOutput = AccessPtr::get(bitmap);

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
      <Pin name="Path" datatype="object:path"/>
      <Pin name="Bitmap" datatype="object:bitmap" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct RenderBitmap final : public PluginEditor
{
    ObjectIn<drawing::api::IBitmap> pinInput;
    In<float> pinOffsetX; // DIPs; shifts the bitmap from centre (e.g. a neumorphic drop-shadow offset)
    In<float> pinOffsetY;

    ReturnCode process() override
    {
        drawingHost->invalidateRect(&bounds);
        return ReturnCode::Ok;
    }

    ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
    {
        if (!pinInput)
            return ReturnCode::Ok;

        Graphics g(drawingContext);
        ClipDrawingToBounds _(g, bounds);

        // wrap the incoming interface so we can draw it.
        Bitmap bitmap;
        if (ReturnCode::Ok != pinInput.value->queryInterface(&drawing::api::IBitmap::guid, AccessPtr::put_void(bitmap)))
            return ReturnCode::Fail;

        const auto size = bitmap.getSize();

        // The bitmap is at physical resolution; its DIP footprint is pixels / rasterizationScale.
        // Draw it centred on the widget so it aligns with other origin-centred layers.
        const float scale = drawingHost ? drawingHost->getRasterizationScale() : 1.0f;
        const float halfW = 0.5f * size.width / scale;
        const float halfH = 0.5f * size.height / scale;

        TempTransform tt(g, makeTranslation(getWidth(bounds) * 0.5f, getHeight(bounds) * 0.5f));

        const float ox = pinOffsetX.value;
        const float oy = pinOffsetY.value;
        const Rect dest{ -halfW + ox, -halfH + oy, halfW + ox, halfH + oy };
        const Rect src{ 0, 0, static_cast<float>(size.width), static_cast<float>(size.height) };
        g.drawBitmap(bitmap, dest, src);

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
      <Pin name="Bitmap" datatype="object:bitmap"/>
      <Pin name="Offset X" datatype="float"/>
      <Pin name="Offset Y" datatype="float"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// Soft shadow/glow of a path. Mode picks where the softness falls (glow vs shadow is then just the
// tint - light reads as a glow, dark as a shadow):
//   Glow         - blur the stroked outline (softness straddles the edge)
//   Drop Shadow  - blur the filled shape, unclipped (interior + halo; sits behind an opaque object)
//   Outer Glow   - blur the filled shape, clip to OUTSIDE the edge (halo only, hollow interior)
//   Inner Shadow - blur the shape's INVERSE, clip to INSIDE the edge (soft rim, recessed look)
// The OUTPUT bitmap is full physical resolution (logical * rasterization
// scale) and sized symmetrically about the origin, so a centred RenderBitmap aligns it with the
// (centred) sharp geometry. The expensive blur runs at a reduced (1/ds) resolution and is then
// upsampled into the full-res output; an integer ds keeps the upscale clean. Downsample = off
// forces full-resolution blur.
struct BlurBitmap final : public GraphicsProcessor
{
    ObjectIn<drawing::api::IPathGeometry> pinPath;
    ObjectIn<IStyle>          pinStyle;
    In<float>                pinBlurRadius;   // in DIPs
    In<bool>                 pinDownsample;
    In<int32_t>              pinMode;         // 0 Glow, 1 Drop Shadow, 2 Outer Glow, 3 Inner Shadow
    ObjectOut<drawing::api::IBitmap> pinOutput;

    Bitmap blurredBitmap;

    BlurBitmap()
    {
        pinBlurRadius.value  = 8.0f;
        pinDownsample.value  = true;
    }

    ReturnCode process() override
    {
        if (!pinPath)
            return ReturnCode::Ok;

        PathGeometry geometry;
        if (ReturnCode::Ok != pinPath.value->queryInterface(&drawing::api::IPathGeometry::guid, AccessPtr::put_void(geometry)))
            return ReturnCode::Fail;

        Factory factory;
        {
            gmpi::shared_ptr<gmpi::api::IUnknown> u;
            drawingHost->getDrawingFactory(u.put());
            u->queryInterface(&drawing::api::IFactory::guid, AccessPtr::put_void(factory));
        }

        // resolve the glow from the same Style interface that Render uses: the stroke colour is
        // the glow tint, the stroke width sets how thick the glowed shape is. Default white/4px.
        gmpi::drawing::Color tint = Colors::White;
        float strokeWidthRaw = 4.0f;
        if (auto* style = pinStyle.value.get())
        {
            style->getStrokeColor(&tint);
            style->getStrokeWidth(&strokeWidthRaw);
        }

        const float scale   = drawingHost ? drawingHost->getRasterizationScale() : 1.0f;
        const float strokeW = (std::max)(0.0f, strokeWidthRaw);
        const float blurDip = (std::max)(0.0f, pinBlurRadius.value);

        // Symmetric extent (DIPs) about the origin: geometry is authored centred on the origin,
        // so the bitmap centre == the geometry origin and a centred draw aligns the layers.
        // 0 Glow (blur the stroke), 1 Drop Shadow (fill, unclipped), 2 Outer Glow (fill, clip to
        // outside the edge), 3 Inner Shadow (blur the INVERSE, clip to inside the edge).
        const bool useFill     = pinMode.value != 0; // everything but Glow fills the shape
        const bool innerShadow = pinMode.value == 3; // blur the inverse of the shape
        const bool clipInside  = pinMode.value == 3; // keep only the soft rim inside the edge
        const bool clipOutside = pinMode.value == 2; // keep only the halo outside the edge

        auto strokeStyle = factory.createStrokeStyle(CapStyle::Round);
        // Fill modes measure the plain shape (stroke width 0); glow measures the widened stroke.
        const Rect wb = geometry.getWidenedBounds(useFill ? 0.0f : strokeW, strokeStyle);
        if (!std::isfinite(wb.left) || !std::isfinite(wb.right) || !std::isfinite(wb.top) || !std::isfinite(wb.bottom))
            return ReturnCode::Ok; // empty/degenerate geometry
        float halfDip = 0.0f;
        for (float v : { std::fabs(wb.left), std::fabs(wb.right), std::fabs(wb.top), std::fabs(wb.bottom) })
            halfDip = (std::max)(halfDip, v);
        halfDip += blurDip + 1.0f;               // room for the blur to fade to zero
        const float dipSize = 2.0f * halfDip;

        const int outDim = (std::max)(1, static_cast<int>(dipSize * scale + 0.5f)); // full physical res

        // The smooth blur runs at outDim / ds. Any integer ds keeps the integer upscale clean.
        int ds = 1;
        if (pinDownsample.value)
        {
            constexpr int kMaxBlurDim = 256;
            ds = (std::max)(1, static_cast<int>(std::ceil(static_cast<float>(outDim) / kMaxBlurDim)));
        }
        const int   loDim   = (std::max)(1, outDim / ds);
        const float loScale = static_cast<float>(loDim) / dipSize; // DIP -> blur-buffer pixel

        // render the stroked path into a low-res mask, drawing in DIPs (origin -> buffer centre).
        std::vector<uint8_t> mask(static_cast<size_t>(loDim) * loDim, 0);
        {
            auto rt = factory.createCpuRenderTarget(SizeU{ static_cast<uint32_t>(loDim), static_cast<uint32_t>(loDim) },
                (int32_t)BitmapRenderTargetFlags::Mask | (int32_t)BitmapRenderTargetFlags::CpuReadable);
            rt.beginDraw();
            rt.setTransform(makeTranslation(halfDip, halfDip) * makeScale(loScale));
            auto brush = rt.createSolidColorBrush(Colors::White);
            if (useFill)
                rt.fillGeometry(geometry, brush);   // solid shape (drop shadow / outer glow / inner shadow)
            else
                rt.drawGeometry(geometry, brush, strokeW, strokeStyle); // glow around the outline
            rt.endDraw();

            auto maskBmp = rt.getBitmap();
            auto d = maskBmp.lockPixels();
            const auto stride = d.getBytesPerRow();
            const uint8_t* srcp = d.getAddress();
            for (int y = 0; y < loDim; ++y)
                std::memcpy(&mask[static_cast<size_t>(y) * loDim], srcp + static_cast<size_t>(y) * stride, loDim);
        }

        // Inner shadow: blur the INVERSE of the shape (keep the sharp shape to clip against after).
        // The blurred inverse bleeds inward past the edge; clipping to the shape leaves a soft rim
        // hugging the inside of the outline - a recessed / pressed look.
        std::vector<uint8_t> shape;
        if (clipInside || clipOutside)
            shape = mask;                                  // sharp shape coverage, kept for the clip
        if (innerShadow)
            for (auto& m : mask) m = static_cast<uint8_t>(255 - m); // blur the inverse

        // blur the mask (radius in blur-buffer pixels => blurDip DIPs once upsampled).
        const unsigned blurPx = static_cast<unsigned>((std::max)(1.0f, blurDip * loScale + 0.5f));
        ginSingleChannel(mask.data(), loDim, loDim, blurPx, loDim);

        if (clipInside) // Inner Shadow: keep only the soft rim inside the shape
        {
            for (size_t i = 0; i < mask.size(); ++i)
                mask[i] = static_cast<uint8_t>((static_cast<int>(mask[i]) * shape[i]) / 255);
        }
        else if (clipOutside) // Outer Glow: erase the interior, keep the halo outside the edge
        {
            for (size_t i = 0; i < mask.size(); ++i)
                mask[i] = static_cast<uint8_t>((static_cast<int>(mask[i]) * (255 - shape[i])) / 255);
        }

        // tint + integer-upsample the blurred mask into the full-res output bitmap.
        blurredBitmap = factory.createImage(outDim, outDim,
            (int32_t)drawing::BitmapRenderTargetFlags::SRGBPixels | (int32_t)drawing::BitmapRenderTargetFlags::CpuReadable);
        {
            auto dd = blurredBitmap.lockPixels(drawing::BitmapLockFlags::Write);
            const auto dstride = dd.getBytesPerRow();
            uint8_t* dst = dd.getAddress();
            const float tintf[3] = { tint.r, tint.g, tint.b };
            const float ta = tint.a;
            constexpr float inv255 = 1.0f / 255.0f;
            for (int y = 0; y < outDim; ++y)
            {
                const int sy = (std::min)(loDim - 1, y / ds);
                uint8_t* drow = dst + static_cast<size_t>(y) * dstride;
                for (int x = 0; x < outDim; ++x)
                {
                    const int sx = (std::min)(loDim - 1, x / ds);
                    const uint8_t a8 = mask[static_cast<size_t>(sy) * loDim + sx];
                    uint8_t* px = drow + x * 4;
                    if (a8 == 0)
                    {
                        px[0] = px[1] = px[2] = px[3] = 0;
                    }
                    else
                    {
                        const float an = a8 * inv255 * ta;
                        for (int j = 0; j < 3; ++j)
                            px[j] = drawing::linearPixelToSRGB(tintf[j] * an);
                        px[3] = static_cast<uint8_t>(255.0f * an + 0.5f);
                    }
                }
            }
        }

        pinOutput = AccessPtr::get(blurredBitmap);
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
      <Pin name="Path" datatype="object:path"/>
      <Pin name="Style" datatype="object:style"/>
      <Pin name="Blur Radius" datatype="float" default="8"/>
      <Pin name="Downsample" datatype="bool" default="1"/>
      <Pin name="Mode" datatype="enum" default="0" metadata="Glow,Drop Shadow,Outer Glow,Inner Shadow"/>
      <Pin name="Blurred" datatype="object:bitmap" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

struct TextFormatNode final : public GraphicsProcessor
{
    ObjectOut<drawing::api::ITextFormat> pinOutput;

    gmpi::drawing::TextFormat textFormat;

    ReturnCode process() override
    {
        if (!pinOutput)
        {
			textFormat = drawingFactory.createTextFormat(20.0f);

            pinOutput = AccessPtr::get(textFormat);
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
      <Pin name="Font" datatype="object:font" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// Renders text to a colour bitmap, sized to the text extent at physical resolution and centred
// on the origin (so a centred RenderBitmap places it in the middle of the control). Mirrors
// BlurBitmap's mask->tint->createImage path, minus the blur.
struct RenderText2Bitmap final : public GraphicsProcessor
{
    ObjectIn<drawing::api::ITextFormat> pinInput;
    In<std::string>                     pinText;
    ObjectIn<IStyle>                    pinStyle;
    ObjectOut<drawing::api::IBitmap>    pinOutput;

    Bitmap bitmap;

    ReturnCode process() override
    {
        if (!pinInput || pinText.value.empty())
            return ReturnCode::Ok;

        TextFormat textFormat;
        if (ReturnCode::Ok != pinInput.value->queryInterface(&drawing::api::ITextFormat::guid, AccessPtr::put_void(textFormat)))
            return ReturnCode::Fail;

        Factory factory;
        {
            gmpi::shared_ptr<gmpi::api::IUnknown> u;
            drawingHost->getDrawingFactory(u.put());
            u->queryInterface(&drawing::api::IFactory::guid, AccessPtr::put_void(factory));
        }

        // text colour comes from the Style's fill (text is a filled shape). Default white.
        gmpi::drawing::Color tint = Colors::White;
        if (auto* style = pinStyle.value.get())
            style->getFillColor(&tint);

        const float scale = drawingHost ? drawingHost->getRasterizationScale() : 1.0f;

        // measure the text (DIPs); pad a little; size symmetric about the origin.
        const Size ext = textFormat.getTextExtentU(pinText.value);
        const float halfW = ext.width * 0.5f + 2.0f;
        const float halfH = ext.height * 0.5f + 2.0f;
        const int   w = (std::max)(1, static_cast<int>(2.0f * halfW * scale + 0.5f));
        const int   h = (std::max)(1, static_cast<int>(2.0f * halfH * scale + 0.5f));

        // render the (white) text into a mask, drawing in DIPs (origin -> buffer centre,
        // text laid out from -ext/2 so it's centred on the origin).
        std::vector<uint8_t> mask(static_cast<size_t>(w) * h, 0);
        {
            auto rt = factory.createCpuRenderTarget(SizeU{ static_cast<uint32_t>(w), static_cast<uint32_t>(h) },
                (int32_t)BitmapRenderTargetFlags::Mask | (int32_t)BitmapRenderTargetFlags::CpuReadable);
            rt.beginDraw();
            rt.setTransform(makeTranslation(halfW, halfH) * makeScale(scale));
            auto brush = rt.createSolidColorBrush(Colors::White);
            rt.drawTextU(pinText.value, textFormat,
                Rect{ -ext.width * 0.5f, -ext.height * 0.5f, ext.width * 0.5f, ext.height * 0.5f }, brush);
            rt.endDraw();

            auto maskBmp = rt.getBitmap();
            auto d = maskBmp.lockPixels();
            const auto stride = d.getBytesPerRow();
            const uint8_t* srcp = d.getAddress();
            for (int y = 0; y < h; ++y)
                std::memcpy(&mask[static_cast<size_t>(y) * w], srcp + static_cast<size_t>(y) * stride, w);
        }

        // tint the coverage with the text colour into a full-res output bitmap.
        bitmap = factory.createImage(w, h,
            (int32_t)drawing::BitmapRenderTargetFlags::SRGBPixels | (int32_t)drawing::BitmapRenderTargetFlags::CpuReadable);
        {
            auto dd = bitmap.lockPixels(drawing::BitmapLockFlags::Write);
            const auto dstride = dd.getBytesPerRow();
            uint8_t* dst = dd.getAddress();
            const float tintf[3] = { tint.r, tint.g, tint.b };
            const float ta = tint.a;
            constexpr float inv255 = 1.0f / 255.0f;
            for (int y = 0; y < h; ++y)
            {
                uint8_t* drow = dst + static_cast<size_t>(y) * dstride;
                const uint8_t* mrow = &mask[static_cast<size_t>(y) * w];
                for (int x = 0; x < w; ++x)
                {
                    const uint8_t a8 = mrow[x];
                    uint8_t* px = drow + x * 4;
                    if (a8 == 0) { px[0] = px[1] = px[2] = px[3] = 0; }
                    else
                    {
                        const float an = a8 * inv255 * ta;
                        for (int j = 0; j < 3; ++j)
                            px[j] = drawing::linearPixelToSRGB(tintf[j] * an);
                        px[3] = static_cast<uint8_t>(255.0f * an + 0.5f);
                    }
                }
            }
        }

        pinOutput = AccessPtr::get(bitmap);
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
      <Pin name="Font" datatype="object:font"/>
      <Pin name="Text" datatype="string_utf8"/>
      <Pin name="Style" datatype="object:style"/>
      <Pin name="Bitmap" datatype="object:bitmap" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}

// 8-bit mask image to 32-bit bitmap.
struct Mask2Bitmap final : public GraphicsProcessor
{
    ObjectIn<drawing::api::IBitmap>  pinInput;
    ObjectOut<drawing::api::IBitmap> pinOutput;

    drawing::Color tint = drawing::colorFromHex(0xffffffu);
    Bitmap blurredBitmap;

    ReturnCode process() override
    {
        if (!pinInput)
            return ReturnCode::Ok;

        auto unknown = pinInput.value.get();

        Bitmap bitmap;

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
          blurredBitmap = factory.createImage(size, (int32_t)drawing::BitmapRenderTargetFlags::SRGBPixels | (int32_t)drawing::BitmapRenderTargetFlags::CpuReadable);
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

        pinOutput = AccessPtr::get(blurredBitmap);

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
      <Pin name="Bitmap8" datatype="object:bitmap"/>
      <Pin name="Bitmap24" datatype="object:bitmap" direction="out"/>
    </GUI>
  </Plugin>
</PluginList>
)XML");
}


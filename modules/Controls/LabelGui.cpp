// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
#include "helpers/GmpiPluginEditor.h"
#include "helpers/ImageMetadata.h"
#include "Extensions/EmbeddedFile.h"

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::drawing;

class LabelGui final : public PluginEditor
{
	Pin<std::string> pinText;
	Pin<std::string> pinStyle;
	Pin<bool>        pinMultiline;
	Pin<int32_t>     pinAlignment;

	gmpi_helper::SkinMetadata skin;
	TextFormat         cachedTextFormat;
	Color              cachedTextColor{ Colors::Black };
	TextAlignment      cachedTextAlignment{ TextAlignment::Leading };
	ParagraphAlignment cachedParagraphAlignment{ ParagraphAlignment::Center };
	Size               cachedTextExtent{};

	Factory getFactory()
	{
		Factory factory;
		if (drawingHost)
		{
			gmpi::shared_ptr<gmpi::api::IUnknown> unk;
			drawingHost->getDrawingFactory(unk.put());
			if (unk)
				unk->queryInterface(&drawing::api::IFactory::guid, AccessPtr::put_void(factory));
		}
		return factory;
	}

	void rebuildTextFormat()
	{
		const auto* fm = skin.getFont(pinStyle.value);

		std::vector<std::string_view> families(fm->faceFamilies_.begin(), fm->faceFamilies_.end());

		// Cap height is driven by the module bounds, not the skin's font-size.
		const float capHeight = (std::max)(1.0f, getHeight(bounds));

		auto factory = getFactory();
		if (!AccessPtr::get(factory))
		{
			cachedTextFormat = {};
			return;
		}

		cachedTextFormat = factory.createTextFormat(
			capHeight,
			families,
			fm->getWeight(),
			fm->getStyle(),
			FontStretch::Normal,
			FontFlags::CapHeight
		);
		switch (pinAlignment.value)
		{
		case 0:  cachedTextAlignment = TextAlignment::Leading;  break;
		case 2:  cachedTextAlignment = TextAlignment::Trailing; break;
		default: cachedTextAlignment = TextAlignment::Center;   break;
		}
		cachedParagraphAlignment = ParagraphAlignment::Center;
		cachedTextFormat.setTextAlignment(cachedTextAlignment);
		cachedTextFormat.setParagraphAlignment(cachedParagraphAlignment);
		cachedTextFormat.setWordWrapping(pinMultiline.value ? WordWrapping::Wrap : WordWrapping::NoWrap);

		cachedTextColor = fm->getColor();

		remeasure();
	}

	void remeasure()
	{
		if (!cachedTextFormat)
		{
			cachedTextExtent = {};
			return;
		}
		const float wrapWidth = pinMultiline.value ? getWidth(bounds) : 100000.0f;
		cachedTextExtent = cachedTextFormat.getTextExtentU(pinText.value, wrapWidth);
	}

	// Layout rect = bounds, expanded outward in the direction(s) the text would overflow,
	// chosen so alignment within the expanded rect anchors the text against the original bounds.
	Rect getLayoutRect() const
	{
		const float overflowW = (std::max)(0.0f, cachedTextExtent.width  - getWidth(bounds));
		const float overflowH = (std::max)(0.0f, cachedTextExtent.height - getHeight(bounds));

		Rect r = bounds;

		switch (cachedTextAlignment)
		{
		case TextAlignment::Leading:  r.right += overflowW; break;
		case TextAlignment::Trailing: r.left  -= overflowW; break;
		case TextAlignment::Center:
		default:
			r.left  -= overflowW * 0.5f;
			r.right += overflowW * 0.5f;
			break;
		}

		switch (cachedParagraphAlignment)
		{
		case ParagraphAlignment::Near: r.bottom += overflowH; break;
		case ParagraphAlignment::Far:  r.top    -= overflowH; break;
		case ParagraphAlignment::Center:
		default:
			r.top    -= overflowH * 0.5f;
			r.bottom += overflowH * 0.5f;
			break;
		}

		return r;
	}

	void redraw()
	{
		if (drawingHost)
		{
			const auto clip = getLayoutRect();
			drawingHost->invalidateRect(&clip);
		}
	}

public:
	LabelGui()
	{
		pinText.onUpdate      = [this](PinBase*) { remeasure(); redraw(); };
		pinStyle.onUpdate     = [this](PinBase*) { rebuildTextFormat(); redraw(); };
		pinMultiline.onUpdate = [this](PinBase*) { rebuildTextFormat(); redraw(); };
		pinAlignment.onUpdate = [this](PinBase*) { rebuildTextFormat(); redraw(); };
	}

	ReturnCode open(gmpi::api::IUnknown* host) override
	{
		const auto r = PluginEditor::open(host);

		// Locate and parse the skin's global.txt for FONT_CATEGORY styles.
		if (auto synthEdit = drawingHost.as<synthedit::IEmbeddedFileSupport>())
		{
			ReturnString globalUri;
			if (synthEdit->findResourceUri("global.txt", &globalUri) == ReturnCode::Ok)
			{
				skin.Serialise(globalUri.c_str());
			}
		}

		return r;
	}

	ReturnCode arrange(const gmpi::drawing::Rect* finalRect) override
	{
		const auto r = PluginEditor::arrange(finalRect);
		rebuildTextFormat();
		return r;
	}

	ReturnCode getClipArea(drawing::Rect* returnRect) override
	{
		*returnRect = getLayoutRect();
		return ReturnCode::Ok;
	}

	ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);

		if (!cachedTextFormat)
			return ReturnCode::Ok;

		auto brush = g.createSolidColorBrush(cachedTextColor);
		g.drawTextU(pinText.value, cachedTextFormat, getLayoutRect(), brush);

		return ReturnCode::Ok;
	}
};

namespace
{
auto r = Register<LabelGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Label" name="Label" category="Sub-Controls">
    <GUI graphicsApi="GmpiUi">
        <Pin name="Text" datatype="string" default="Label"/>
        <Pin name="Style" datatype="string" default="control_label"/>
        <Pin name="Multiline" datatype="bool"/>
        <Pin name="Alignment" datatype="enum" default="1" metadata="Left,Center,Right"/>
    </GUI>
</Plugin>
)XML");
}

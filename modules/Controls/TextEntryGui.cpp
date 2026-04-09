// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.

#include "ControlsBase.h"
#include "helpers/NativeUi.h"

class TextEntryGui final : public ControlsBase
{
	Pin<std::string> pinpatchValue;
	Pin<std::string> pinHint;

	TextFormat cachedTextFormat;

	TextFormat& getTextFormat(Graphics& g)
	{
		if (!cachedTextFormat)
		{
			cachedTextFormat = g.getFactory().createTextFormat(
				12.0f,
				std::vector<std::string_view>{"Segoe UI", "Arial"}
			);
			cachedTextFormat.setTextAlignment(TextAlignment::Leading);
			cachedTextFormat.setParagraphAlignment(ParagraphAlignment::Center);
		}
		return cachedTextFormat;
	}

	void startTextEdit()
	{
		if (!dialogHost)
			return;

		gmpi::shared_ptr<gmpi::api::IUnknown> unk;
		if (dialogHost->createTextEdit(&bounds, unk.put()) != ReturnCode::Ok || !unk)
			return;

		auto textEdit = unk.as<gmpi::api::ITextEdit>();
		if (!textEdit)
			return;

		textEdit->setText(pinpatchValue.value.c_str());
		textEdit->setTextSize(12.0f);

		textEditCallback = gmpi::sdk::TextEditCallback([this](const std::string& newText) {
			pinpatchValue = newText;
			redraw();
		});
		nativeTextEdit = textEdit;
		textEdit->showAsync(static_cast<gmpi::api::IUnknown*>(&textEditCallback));
	}

	gmpi::sdk::TextEditCallback textEditCallback;
	gmpi::shared_ptr<gmpi::api::ITextEdit> nativeTextEdit;

public:
	TextEntryGui()
	{
		pinpatchValue.onUpdate = [this](PinBase*) { redraw(); };
	}

	ReturnCode render(drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);

		ClipDrawingToBounds _(g, bounds);

		const auto r = bounds;
		const float w = getWidth(r);
		const float h = getHeight(r);

		// Background
		const Rect bodyRect{ 0.0f, 0.0f, w, h };
		auto bgBrush = g.createSolidColorBrush(Colors::White);
		g.fillRectangle(bodyRect, bgBrush);

		// Border
		auto borderBrush = g.createSolidColorBrush(Color{ 0.6f, 0.6f, 0.6f, 1.0f });
		g.drawRectangle(bodyRect, borderBrush, 1.0f);

		// Text
		const float margin = 4.0f;
		const Rect textRect{ margin, 0.0f, w - margin, h };
		auto textBrush = g.createSolidColorBrush(Colors::Black);
		auto& tf = getTextFormat(g);
		g.drawTextU(pinpatchValue.value, tf, textRect, textBrush);

		return ReturnCode::Ok;
	}

	ReturnCode onPointerDown(Point point, int32_t flags) override
	{
		if ((flags & static_cast<int32_t>(gmpi::api::GG_POINTER_FLAG_FIRSTBUTTON)) == 0)
			return ReturnCode::Unhandled;

		startTextEdit();
		return ReturnCode::Ok;
	}
/* TODO
	ReturnCode getToolTip(Point, gmpi::api::IString* returnString) override
	{
		returnString->setData(pinHint.value.data(), static_cast<int32_t>(pinHint.value.size()));
		return ReturnCode::Ok;
	}
*/
};

namespace
{
auto r = gmpi::Register<TextEntryGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Text EntryG" name="Text Entry" category="Sub-Controls">
    <Parameters>
        <Parameter id="0" datatype="string" name="patchValue"/>
    </Parameters>
    <Audio>
        <Pin name="patchValue" datatype="string" default="" private="true" parameterId="0"/>
        <Pin name="Text Out" datatype="string" direction="out" autoConfigureParameter="true"/>
    </Audio>
    <GUI graphicsApi="GmpiGui">
        <Pin name="patchValue" datatype="string" private="true" parameterId="0"/>
        <Pin name="Hint" datatype="string" isMinimised="true"/>
    </GUI>
</Plugin>
)XML");
}

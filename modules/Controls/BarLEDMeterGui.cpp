// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.

#include "helpers/GmpiPluginEditor.h"
#include "helpers/CachedBlur.h"
#include <algorithm>
#include <cmath>

using namespace gmpi;
using namespace gmpi::drawing;
using namespace gmpi::editor;

class BarLEDMeterGui final : public PluginEditor
{
	cachedBlur shadowBlur;
	SizeU shadowSize{};

	Pin<float> pinNormalized;
	Pin<bool> pinRed;
	Pin<bool> pinGreen;
	Pin<bool> pinBlue;
	Pin<float> pinRadius;

	static constexpr int ledCount = 12;
	static constexpr float ledGap = 2.0f;

	Color getLedOnColor() const
	{
		return {
			static_cast<float>(pinRed.value),
			static_cast<float>(pinGreen.value),
			static_cast<float>(pinBlue.value),
			1.0f
		};
	}

	bool isVertical() const
	{
		return getHeight(bounds) > getWidth(bounds);
	}

	Rect getLedRect(int index, const Rect& localBounds) const
	{
		const float w = getWidth(localBounds);
		const float h = getHeight(localBounds);

		if (isVertical())
		{
			const float totalGap = ledGap * static_cast<float>(ledCount - 1);
			const float ledH = (h - totalGap) / static_cast<float>(ledCount);
			// index 0 = bottom LED (lowest value)
			const float top = localBounds.bottom - (static_cast<float>(index + 1) * ledH + static_cast<float>(index) * ledGap);
			return { localBounds.left, top, localBounds.right, top + ledH };
		}
		else
		{
			const float totalGap = ledGap * static_cast<float>(ledCount - 1);
			const float ledW = (w - totalGap) / static_cast<float>(ledCount);
			// index 0 = left LED (lowest value)
			const float left = localBounds.left + static_cast<float>(index) * (ledW + ledGap);
			return { left, localBounds.top, left + ledW, localBounds.bottom };
		}
	}

	void updateShadowCache(const SizeU& size)
	{
		if (shadowSize.width != size.width || shadowSize.height != size.height)
		{
			shadowBlur.invalidate();
			shadowSize = size;
		}
	}

	void redraw()
	{
		if (drawingHost)
			drawingHost->invalidateRect(&bounds);
	}

	void onSetColor()
	{
		shadowBlur.invalidate();
		redraw();
	}

public:
	BarLEDMeterGui()
	{
		pinNormalized.onUpdate = [this](PinBase*) { redraw(); };
		pinRed.onUpdate = [this](PinBase*) { onSetColor(); };
		pinGreen.onUpdate = [this](PinBase*) { onSetColor(); };
		pinBlue.onUpdate = [this](PinBase*) { onSetColor(); };
		pinRadius.onUpdate = [this](PinBase*) { shadowBlur.invalidate(); redraw(); };
	}

	ReturnCode measure(const Size* availableSize, Size* returnDesiredSize) override
	{
		*returnDesiredSize = *availableSize;
		returnDesiredSize->width = (std::max)(4.0f, returnDesiredSize->width);
		returnDesiredSize->height = (std::max)(4.0f, returnDesiredSize->height);
		return ReturnCode::Ok;
	}

	ReturnCode render(drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);
		ClipDrawingToBounds clip(g, bounds);

		const auto width = (std::max)(1u, static_cast<uint32_t>(getWidth(bounds)));
		const auto height = (std::max)(1u, static_cast<uint32_t>(getHeight(bounds)));
		const Rect localBounds{ 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height) };

		const float normalized = (std::clamp)(pinNormalized.value, 0.0f, 1.0f);
		const float litLeds = normalized * static_cast<float>(ledCount);
		const float cornerRadius = pinRadius.value;
		const auto onColor = getLedOnColor();
		const auto offColor = interpolateColor(onColor, Colors::Black, 0.75f);

		// Shadow behind the whole meter
		{
			updateShadowCache({ width, height });
			shadowBlur.tint = Color{ 0.0f, 0.0f, 0.0f, 0.35f };
			shadowBlur.blurRadius = 3;

			const auto orig = g.getTransform();
			g.setTransform(makeTranslation({ 1.5f, 1.5f }) * orig);
			shadowBlur.draw(g, localBounds, [&](Graphics& mask)
				{
					auto brush = mask.createSolidColorBrush(Colors::White);
					for (int i = 0; i < ledCount; ++i)
					{
						const auto r = getLedRect(i, localBounds);
						mask.fillRoundedRectangle({ r, cornerRadius, cornerRadius }, brush);
					}
				});
			g.setTransform(orig);
		}

		// Draw LEDs
		auto strokeBrush = g.createSolidColorBrush(Color{ 0.15f, 0.15f, 0.15f, 0.5f });
		const float strokeWidth = 0.5f;

		for (int i = 0; i < ledCount; ++i)
		{
			const auto ledRect = getLedRect(i, localBounds);
			const float ledIndex = static_cast<float>(i);

			// brightness: fully on, partial, or off
			float brightness;
			if (ledIndex + 1.0f <= litLeds)
				brightness = 1.0f;
			else if (ledIndex < litLeds)
				brightness = litLeds - ledIndex;
			else
				brightness = 0.0f;

			const auto color = interpolateColor(offColor, onColor, brightness);
			const RoundedRect rr{ ledRect, cornerRadius, cornerRadius };

			auto fillBrush = g.createSolidColorBrush(color);
			g.fillRoundedRectangle(rr, fillBrush);

			// bright core highlight when lit
			if (brightness > 0.1f)
			{
				const float inset = (std::min)(getWidth(ledRect), getHeight(ledRect)) * 0.2f;
				const Rect coreRect{
					ledRect.left + inset,
					ledRect.top + inset,
					ledRect.right - inset,
					ledRect.bottom - inset
				};
				const float coreRadius = (std::max)(0.0f, cornerRadius - inset);
				auto coreColor = interpolateColor(color, Colors::White, 0.5f * brightness);
				coreColor.a = brightness * 0.6f;
				auto coreBrush = g.createSolidColorBrush(coreColor);
				g.fillRoundedRectangle({ coreRect, coreRadius, coreRadius }, coreBrush);
			}

			g.drawRoundedRectangle(rr, strokeBrush, strokeWidth);
		}

		return ReturnCode::Ok;
	}
};

namespace
{
auto r = gmpi::Register<BarLEDMeterGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Bar LED Meter" name="Bar LED Meter" category="Sub-Controls">
    <GUI graphicsApi="GmpiGui">
        <Pin name="Normalized" datatype="float" default="0.0"/>
        <Pin name="Red" datatype="bool"/>
        <Pin name="Green" datatype="bool"/>
        <Pin name="Blue" datatype="bool"/>
        <Pin name="Radius" datatype="float" default="2"/>
    </GUI>
</Plugin>
)XML");
}

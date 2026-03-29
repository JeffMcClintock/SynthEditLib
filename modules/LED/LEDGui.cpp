/* Copyright (c) 2007-2026 SynthEdit Ltd

Permission to use, copy, modify, and /or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS.IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
#include "helpers/GmpiPluginEditor.h"
#include "helpers/BitmapMask.h"

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::drawing;

class LEDGui final : public PluginEditor, public gmpi::api::IDrawingLayer
{
	Color getLedColor(float brightness) const
	{
		return interpolateColor(Colors::DimGray, Colors::Lime, brightness);
	}

	Color getLedCoreColor(float brightness) const
	{
		return interpolateColor(getLedColor(brightness), Colors::White, 0.75f * brightness);
	}

	Rect getGlowRect() const
	{
		const auto center = getCenter(bounds);
		const auto diameter = (std::min)(getWidth(bounds), getHeight(bounds));
		const auto spriteRadius = glowSize + std::ceil(diameter * 0.5f);

		return {
			center.x - spriteRadius,
			center.y - spriteRadius,
			center.x + spriteRadius,
			center.y + spriteRadius
		};
	}

	void redraw()
	{
		const auto r = getGlowRect();
		drawingHost->invalidateRect(&r);
	}
	void onSetAnimationPosition()
	{
		redraw();
	}

	void onSetBackground()
	{
		redraw();
	}

	void onSetForground()
	{
		redraw();
	}

	Pin<float> pinAnimationPosition;
	Pin<std::string> pinFill;
	Pin<std::string> pinStroke;

	static const int glowSize = 100;

public:
	LEDGui()
	{
		pinAnimationPosition.onUpdate = [this](PinBase*) { onSetAnimationPosition(); };
		pinFill.onUpdate = [this](PinBase*) { onSetBackground(); };
		pinStroke.onUpdate = [this](PinBase*) { onSetForground(); };
	}

	int32_t addRef() override
	{
		return PluginEditor::addRef();
	}

	int32_t release() override
	{
		return PluginEditor::release();
	}

	ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);
//		ClipDrawingToBounds _(g, bounds);

		const auto center = getCenter(bounds);
		const auto radius = 0.5f * (std::min)(getWidth(bounds), getHeight(bounds));
		const auto brightness = std::clamp(pinAnimationPosition.value, 0.0f, 1.0f);
		const auto ledColor = getLedColor(brightness);

		auto fill = g.createSolidColorBrush(ledColor);
		auto stroke = g.createSolidColorBrush(Colors::Black);
		g.fillCircle(center, radius, fill);

		if(brightness > 0.0f)
		{
			auto core = g.createSolidColorBrush(Color{1.f, 1.f, 1.f, 0.5f});
			g.fillCircle(center, radius * (0.85f + 0.1f * brightness), core);
		}

		g.drawCircle(center, radius, stroke);

		return ReturnCode::Ok;
	}

	ReturnCode getClipArea(gmpi::drawing::Rect* returnRect) override
	{
		*returnRect = getGlowRect();
		return ReturnCode::Ok;
	}

	ReturnCode renderLayer(gmpi::drawing::api::IDeviceContext* drawingContext, int32_t layer) override
	{
		if (layer == 0)
			return render(drawingContext);

		if (layer != 1)
			return ReturnCode::NoSupport;

		constexpr float falloff = 0.3f; // bigger = steeper (less glow)
		const auto brightness = std::clamp(pinAnimationPosition.value, 0.0f, 1.0f);
		if (brightness <= 0.0f)
			return ReturnCode::Ok;

		const auto ledColor = getLedColor(brightness);

        Graphics g(drawingContext);

		const auto diameter = (std::min)(getWidth(bounds), getHeight(bounds));
		const auto bitmapSize = 2 * glowSize + static_cast<int32_t>(std::ceil(diameter));
		const auto glowRect = getGlowRect();

		auto glowBitmap = g.getFactory().createImage(bitmapSize, bitmapSize);
		
		const auto centerRadius = (std::max)(1.0f, diameter * 0.5f);
		const auto spriteRadius = 0.5f * static_cast<float>(bitmapSize);

		// create a scope so that pixels is automatically released to unlock the image we're done.
		{
			// create a 'glow' bitmap using purely additive pixel values.
			auto pixels = glowBitmap.lockPixels(BitmapLockFlags::Write);
			if(!pixels)
				return ReturnCode::Fail;

			uint8_t* data = pixels.getAddress();
			const auto bytesPerRow = pixels.getBytesPerRow();
			constexpr float taperZoneStart = 0.5f;
			constexpr float taperGradient = 1.0f / (1.0f - taperZoneStart);
			const auto red = ledColor.r * brightness;
			const auto green = ledColor.g * brightness;
			const auto blue = ledColor.b * brightness;

			const auto zero = detail::floatToHalf(0.0f);

			for(int32_t y = 0; y < bitmapSize; ++y)
			{
				auto row = reinterpret_cast<uint16_t*>(data + y * bytesPerRow);
				for(int32_t x = 0; x < bitmapSize; ++x)
				{
					const auto dx = (x + 0.5f) - spriteRadius;
					const auto dy = (y + 0.5f) - spriteRadius;
					const auto distance = std::sqrt(dx * dx + dy * dy);
					auto glow = 0.5f / (std::max)(1.0f, falloff * (distance - centerRadius));
					glow *= (std::clamp)(1.0f - (taperGradient * (distance - spriteRadius * taperZoneStart) / spriteRadius), 0.0f, 1.0f);

					row[x * 4 + 0] = detail::floatToHalf(red * glow);
					row[x * 4 + 1] = detail::floatToHalf(green * glow);
					row[x * 4 + 2] = detail::floatToHalf(blue * glow);
					row[x * 4 + 3] = zero;
				}
			}
		}

     const gmpi::drawing::Rect srcRect{ 0.0f, 0.0f, static_cast<float>(bitmapSize), static_cast<float>(bitmapSize) };
     g.drawBitmap(glowBitmap, glowRect, srcRect);

		return ReturnCode::Ok;
	}

	ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
	{
        *returnInterface = {};

		if ((*iid) == gmpi::api::IDrawingLayer::guid)
		{
			*returnInterface = static_cast<gmpi::api::IDrawingLayer*>(this);
			PluginEditor::addRef();
			return ReturnCode::Ok;
		}

		return PluginEditor::queryInterface(iid, returnInterface);
	}
};

namespace
{
auto r = gmpi::Register<LEDGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE LED" name="LED" category="Sub-Controls">
    <GUI graphicsApi="GmpiGui">
        <Pin name="Animation Position" datatype="float"/>
        <Pin name="Fill" datatype="string"/>
        <Pin name="Stroke" datatype="string"/>
    </GUI>
</Plugin>
)XML");
}

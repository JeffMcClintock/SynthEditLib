// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.

#include "helpers/GmpiPluginEditor.h"
#include "helpers/BitmapMask.h"
#include <algorithm>
#include <cmath>

using namespace gmpi;
using namespace gmpi::drawing;
using namespace gmpi::editor;

class BarLEDMeterGui final : public PluginEditor, public gmpi::api::IDrawingLayer
{
	Bitmap glowBitmap;
	int32_t glowBitmapWidth = 0;
	int32_t glowBitmapHeight = 0;
	bool glowBitmapDirty = true;

	Pin<float> pinNormalized;
	Pin<bool> pinRed;
	Pin<bool> pinGreen;
	Pin<bool> pinBlue;
	Pin<float> pinRadius;

	static constexpr int ledCount = 12;
	static constexpr float ledGap = 2.0f;
	static constexpr int glowPadding = 40;

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

	// returns the rect of a single LED in local coordinates (relative to bounds origin)
	Rect getLedRect(int index) const
	{
		const float w = getWidth(bounds);
		const float h = getHeight(bounds);

		if (isVertical())
		{
			const float totalGap = ledGap * static_cast<float>(ledCount - 1);
			const float ledH = (h - totalGap) / static_cast<float>(ledCount);
			// index 0 = bottom LED (lowest value)
			const float top = h - (static_cast<float>(index + 1) * ledH + static_cast<float>(index) * ledGap);
			return { 0.0f, top, w, top + ledH };
		}
		else
		{
			const float totalGap = ledGap * static_cast<float>(ledCount - 1);
			const float ledW = (w - totalGap) / static_cast<float>(ledCount);
			// index 0 = left LED (lowest value)
			const float left = static_cast<float>(index) * (ledW + ledGap);
			return { left, 0.0f, left + ledW, h };
		}
	}

	// size of a single LED (all LEDs are the same size)
	Size getLedSize() const
	{
		const auto r = getLedRect(0);
		return { getWidth(r), getHeight(r) };
	}

	Rect getGlowClipRect() const
	{
		return {
			bounds.left - static_cast<float>(glowPadding),
			bounds.top - static_cast<float>(glowPadding),
			bounds.right + static_cast<float>(glowPadding),
			bounds.bottom + static_cast<float>(glowPadding)
		};
	}

	void redraw()
	{
		if (drawingHost)
		{
			const auto r = getGlowClipRect();
			drawingHost->invalidateRect(&r);
		}
	}

	void onSetColor()
	{
		glowBitmapDirty = true;
		redraw();
	}

	// Create a glow bitmap for a single LED, sized to ledSize + glowPadding on each side.
	// Uses additive half-float pixels (alpha=0) just like LEDGui.
	ReturnCode updateGlowBitmap(Graphics& g, int32_t bmpW, int32_t bmpH)
	{
		if (!glowBitmapDirty && glowBitmap && glowBitmapWidth == bmpW && glowBitmapHeight == bmpH)
			return ReturnCode::Ok;

		glowBitmap = g.getFactory().createImage(bmpW, bmpH);
		glowBitmapWidth = bmpW;
		glowBitmapHeight = bmpH;
		glowBitmapDirty = false;

		const auto ledSize = getLedSize();
		const float halfW = static_cast<float>(bmpW) * 0.5f;
		const float halfH = static_cast<float>(bmpH) * 0.5f;
		const float ledHalfW = ledSize.width * 0.5f;
		const float ledHalfH = ledSize.height * 0.5f;
		const float cornerRadius = pinRadius.value;
		const auto ledColor = getLedOnColor();

		{
			auto pixels = glowBitmap.lockPixels(BitmapLockFlags::Write);
			if (!pixels)
				return ReturnCode::Fail;

			constexpr float falloff = 0.4f;
			constexpr float taperZoneStart = 0.5f;
			constexpr float taperGradient = 1.0f / (1.0f - taperZoneStart);
			const float maxDist = static_cast<float>(glowPadding);

			for (auto& it : pixelIterator<RgbaHalfPixel>(pixels))
			{
				// pixel position relative to centre of the bitmap
				const float px = (it.x + 0.5f) - halfW;
				const float py = (it.y + 0.5f) - halfH;

				// distance from the rounded rectangle surface
				// first, distance from the inner rectangle (shrunk by corner radius)
				const float innerHalfW = (std::max)(0.0f, ledHalfW - cornerRadius);
				const float innerHalfH = (std::max)(0.0f, ledHalfH - cornerRadius);

				const float dx = (std::max)(0.0f, std::abs(px) - innerHalfW);
				const float dy = (std::max)(0.0f, std::abs(py) - innerHalfH);
				const float distFromInner = std::sqrt(dx * dx + dy * dy);
				const float distFromSurface = (std::max)(0.0f, distFromInner - cornerRadius);

				// glow intensity: bright at surface, falling off with distance
				float glow = 0.5f / (std::max)(1.0f, falloff * distFromSurface);

				// taper to zero at the edge of the bitmap
				const float normalizedDist = distFromSurface / (std::max)(1.0f, maxDist);
				glow *= (std::clamp)(1.0f - taperGradient * (normalizedDist - taperZoneStart), 0.0f, 1.0f);

				// inside the LED shape, full glow
				if (distFromInner <= cornerRadius)
					glow = (std::max)(glow, 0.5f);

				it->setR(ledColor.r * glow);
				it->setG(ledColor.g * glow);
				it->setB(ledColor.b * glow);
				it->setA(0.0f); // additive blending
			}
		}

		return ReturnCode::Ok;
	}

public:
	BarLEDMeterGui()
	{
		pinNormalized.onUpdate = [this](PinBase*) { redraw(); };
		pinRed.onUpdate = [this](PinBase*) { onSetColor(); };
		pinGreen.onUpdate = [this](PinBase*) { onSetColor(); };
		pinBlue.onUpdate = [this](PinBase*) { onSetColor(); };
		pinRadius.onUpdate = [this](PinBase*) { glowBitmapDirty = true; redraw(); };
	}

	int32_t addRef() override
	{
		return PluginEditor::addRef();
	}

	int32_t release() override
	{
		return PluginEditor::release();
	}

	ReturnCode measure(const Size* availableSize, Size* returnDesiredSize) override
	{
		*returnDesiredSize = *availableSize;
		returnDesiredSize->width = (std::max)(4.0f, returnDesiredSize->width);
		returnDesiredSize->height = (std::max)(4.0f, returnDesiredSize->height);
		return ReturnCode::Ok;
	}

	ReturnCode getClipArea(Rect* returnRect) override
	{
		*returnRect = getGlowClipRect();
		return ReturnCode::Ok;
	}

	ReturnCode renderLayer(drawing::api::IDeviceContext* drawingContext, int32_t layer) override
	{
		if (layer == 0)
		{
			Graphics g(drawingContext);

			const float normalized = (std::clamp)(pinNormalized.value, 0.0f, 1.0f);
			const float litLeds = normalized * static_cast<float>(ledCount);
			const float cornerRadius = pinRadius.value;
			const auto onColor = Colors::White;
			const auto offColor = interpolateColor(Colors::Black, getLedOnColor(), 0.307f);
			auto onBrush = g.createSolidColorBrush(onColor);
			auto offBrush = g.createSolidColorBrush(offColor);

			auto strokeBrush = g.createSolidColorBrush(Color{ 0.15f, 0.15f, 0.15f, 0.5f });

			for (int i = 0; i < ledCount; ++i)
			{
				const auto ledRect = getLedRect(i);
				const float ledIndex = static_cast<float>(i);

				const RoundedRect rr{ ledRect, cornerRadius, cornerRadius };

				g.fillRoundedRectangle(rr, ledIndex + 1.0f <= litLeds ? onBrush : offBrush);
				g.drawRoundedRectangle(rr, strokeBrush, 0.5f);
			}

			return ReturnCode::Ok;
		}

		if (layer != 1)
			return ReturnCode::NoSupport;

		const float normalized = (std::clamp)(pinNormalized.value, 0.0f, 1.0f);
		if (normalized <= 0.0f)
			return ReturnCode::Ok;

		Graphics g(drawingContext);

		const auto ledSize = getLedSize();
		const int32_t bmpW = 2 * glowPadding + static_cast<int32_t>(std::ceil(ledSize.width));
		const int32_t bmpH = 2 * glowPadding + static_cast<int32_t>(std::ceil(ledSize.height));

		auto rc = updateGlowBitmap(g, bmpW, bmpH);
		if (rc != ReturnCode::Ok)
			return rc;

		const Rect srcRect{ 0.0f, 0.0f, static_cast<float>(bmpW), static_cast<float>(bmpH) };
		const float litLeds = normalized * static_cast<float>(ledCount);
		const float pad = static_cast<float>(glowPadding);
		float brightness = 0.5f; // todo make it a ppin
		for (int i = 0; i < ledCount; ++i)
		{
			const float ledIndex = static_cast<float>(i);

			if (ledIndex + 1.0f > litLeds)
				continue; // unlit — skip

			const auto ledRect = getLedRect(i);
			const Rect destRect{
				ledRect.left - pad,
				ledRect.top - pad,
				ledRect.right + pad,
				ledRect.bottom + pad
			};

			g.drawBitmap(glowBitmap, destRect, srcRect, brightness);
		}

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

// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.

#include "helpers/GmpiPluginEditor.h"
#include "helpers/BitmapMask.h"

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::drawing;

class LEDGui final : public PluginEditor, public gmpi::api::IDrawingLayer
{
	Bitmap glowBitmap;
	int32_t glowBitmapSize = 0;
	float glowBitmapDiameter = 0.0f;
	bool glowBitmapDirty = true;

	Color getLedColor(float brightness) const
	{
		const Color targetColor{
			   static_cast<float>(pinRed.value),
			   static_cast<float>(pinGreen.value),
			   static_cast<float>(pinBlue.value),
			   1.0f
		};

		return interpolateColor(Colors::Black, targetColor, brightness);
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

	void onSetColor()
	{
		glowBitmapDirty = true;
		redraw();
	}

	ReturnCode updateGlowBitmap(Graphics& g, int32_t bitmapSize, float diameter)
	{
		if(!glowBitmapDirty && glowBitmap && glowBitmapSize == bitmapSize && glowBitmapDiameter == diameter)
			return ReturnCode::Ok;

		glowBitmap = g.getFactory().createImage(bitmapSize, bitmapSize);
		glowBitmapSize = bitmapSize;
		glowBitmapDiameter = diameter;
		glowBitmapDirty = false;

		const auto centerRadius = (std::max)(1.0f, diameter * 0.5f);
		const auto spriteRadius = 0.5f * static_cast<float>(bitmapSize);
		const auto ledColor = getLedColor(1.0f);

		// create a scope so that pixels is automatically released to unlock the image we're done.
		{
			// create a 'glow' bitmap using purely additive pixel values.
			auto pixels = glowBitmap.lockPixels(BitmapLockFlags::Write);
			if(!pixels)
				return ReturnCode::Fail;

			constexpr float falloff = 0.3f; // bigger = steeper (less glow)
			constexpr float taperZoneStart = 0.5f;
			constexpr float taperGradient = 1.0f / (1.0f - taperZoneStart);
			constexpr float starPoints = 6.0f;
			constexpr float starAxisCount = 0.5f * starPoints;
			constexpr float starWidth = 6.0f;
			constexpr float starStrength = 2.0f;
			const auto red = ledColor.r;
			const auto green = ledColor.g;
			const auto blue = ledColor.b;

			for(auto& it : pixelIterator<RgbaHalfPixel>(pixels))
			{
				const auto dx = (it.x + 0.5f) - spriteRadius;
				const auto dy = (it.y + 0.5f) - spriteRadius;
				const auto distance = std::sqrt(dx * dx + dy * dy);
				const auto angle = std::atan2(dy, dx);
				const auto radialBlend = (std::clamp)((distance - centerRadius) / (std::max)(1.0f, spriteRadius - centerRadius), 0.0f, 1.0f);
				const auto spokeOffset = distance * std::sin(starAxisCount * angle);
				const auto star = std::exp(-(spokeOffset * spokeOffset) / (2.0f * starWidth * starWidth));
				auto glow = 0.5f / (std::max)(1.0f, falloff * (distance - centerRadius));
				glow *= (std::clamp)(1.0f - (taperGradient * (distance - spriteRadius * taperZoneStart) / spriteRadius), 0.0f, 1.0f);
				glow *= 1.0f + starStrength * radialBlend * star;

				it->setR(red * glow);
				it->setG(green * glow);
				it->setB(blue * glow);
				it->setA(0.0f);
			}
		}

		return ReturnCode::Ok;
	}

	Pin<float> pinAnimationPosition;
	Pin<bool> pinRed;
	Pin<bool> pinGreen;
	Pin<bool> pinBlue;

	static const int glowSize = 100;

public:
	LEDGui()
	{
		pinAnimationPosition.onUpdate = [this](PinBase*) { onSetAnimationPosition(); };
		pinRed.onUpdate = [this](PinBase*) { onSetColor(); };
		pinGreen.onUpdate = [this](PinBase*) { onSetColor(); };
		pinBlue.onUpdate = [this](PinBase*) { onSetColor(); };
	}

	int32_t addRef() override
	{
		return PluginEditor::addRef();
	}

	int32_t release() override
	{
		return PluginEditor::release();
	}

	ReturnCode getClipArea(Rect* returnRect) override
	{
		*returnRect = getGlowRect();
		return ReturnCode::Ok;
	}

	ReturnCode renderLayer(drawing::api::IDeviceContext* drawingContext, int32_t layer) override
	{
		if(layer == 0)
		{
			Graphics g(drawingContext);

			const auto center = getCenter(bounds);
			const auto radius = 0.5f * (std::min)(getWidth(bounds), getHeight(bounds));
			const auto brightness = std::clamp(pinAnimationPosition.value, 0.0f, 1.0f);
			const auto unlit = getLedColor(0.307f);

			auto fill = g.createSolidColorBrush(interpolateColor(unlit, Colors::White, brightness));
			g.fillCircle(center, radius, fill);

			auto stroke = g.createSolidColorBrush(Color{ 0.3f, 0.3f, 0.3f, 1.0f });
			g.drawCircle(center, radius, stroke);

			return ReturnCode::Ok;
		}

		if(layer != 1)
			return ReturnCode::NoSupport;

		const auto brightness = std::clamp(pinAnimationPosition.value, 0.0f, 1.0f);
		if(brightness <= 0.0f)
			return ReturnCode::Ok;

		Graphics g(drawingContext);

		const auto diameter = (std::min)(getWidth(bounds), getHeight(bounds));
		const auto bitmapSize = 2 * glowSize + static_cast<int32_t>(std::ceil(diameter));
		const auto glowRect = getGlowRect();

		auto rc = updateGlowBitmap(g, bitmapSize, diameter);
		if(rc != ReturnCode::Ok)
			return rc;

		const Rect srcRect{ 0.0f, 0.0f, static_cast<float>(bitmapSize), static_cast<float>(bitmapSize) };
		g.drawBitmap(glowBitmap, glowRect, srcRect, brightness);

		return ReturnCode::Ok;
	}

	ReturnCode queryInterface(const gmpi::api::Guid* iid, void** returnInterface) override
	{
		*returnInterface = {};

		if((*iid) == gmpi::api::IDrawingLayer::guid)
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
        <Pin name="Red" datatype="bool"/>
		<Pin name="Green" datatype="bool"/>
		<Pin name="Blue" datatype="bool"/>
    </GUI>
</Plugin>
)XML");
}

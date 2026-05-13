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
	int32_t glowBitmapWidth = 0;
	int32_t glowBitmapHeight = 0;
	float glowBitmapCornerRadius = 0.0f;
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

	float getClampedCornerRadius() const
	{
		const auto halfMin = 0.5f * (std::min)(getWidth(bounds), getHeight(bounds));
		return (std::clamp)(static_cast<float>(pinCornerRadius.value), 0.0f, halfMin);
	}

	// Signed distance from point (dx,dy) — measured from the LED rect's center —
	// to the boundary of a rounded rectangle with half-extents (halfW, halfH) and corner radius r.
	// Negative inside, positive outside. Degenerates to the circle SDF when halfW==halfH==r.
	static float roundedRectSdf(float dx, float dy, float halfW, float halfH, float r)
	{
		const auto qx = std::abs(dx) - halfW + r;
		const auto qy = std::abs(dy) - halfH + r;
		const auto outside = std::sqrt((std::max)(qx, 0.0f) * (std::max)(qx, 0.0f) + (std::max)(qy, 0.0f) * (std::max)(qy, 0.0f));
		const auto inside = (std::min)((std::max)(qx, qy), 0.0f);
		return outside + inside - r;
	}

	Rect getGlowRect() const
	{
		return inflateRect(bounds, static_cast<float>(glowSize));
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

	void onSetCornerRadius()
	{
		glowBitmapDirty = true;
		redraw();
	}

	ReturnCode updateGlowBitmap(Graphics& g, int32_t bitmapWidth, int32_t bitmapHeight, float ledWidth, float ledHeight, float cornerRadius)
	{
		if(!glowBitmapDirty && glowBitmap
			&& glowBitmapWidth == bitmapWidth
			&& glowBitmapHeight == bitmapHeight
			&& glowBitmapCornerRadius == cornerRadius)
			return ReturnCode::Ok;

		glowBitmap = g.getFactory().createImage(bitmapWidth, bitmapHeight);
		glowBitmapWidth = bitmapWidth;
		glowBitmapHeight = bitmapHeight;
		glowBitmapCornerRadius = cornerRadius;
		glowBitmapDirty = false;

		const auto halfW = (std::max)(0.5f, ledWidth * 0.5f);
		const auto halfH = (std::max)(0.5f, ledHeight * 0.5f);
		const auto bitmapCenterX = 0.5f * static_cast<float>(bitmapWidth);
		const auto bitmapCenterY = 0.5f * static_cast<float>(bitmapHeight);
		const auto maxSpriteRadius = (std::max)(bitmapCenterX, bitmapCenterY);
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
				const auto dx = (it.x + 0.5f) - bitmapCenterX;
				const auto dy = (it.y + 0.5f) - bitmapCenterY;
				const auto distance = std::sqrt(dx * dx + dy * dy);
				const auto angle = std::atan2(dy, dx);
				const auto edgeDist = roundedRectSdf(dx, dy, halfW, halfH, cornerRadius);
				const auto radialBlend = (std::clamp)(edgeDist / (std::max)(1.0f, maxSpriteRadius - cornerRadius), 0.0f, 1.0f);
				const auto spokeOffset = distance * std::sin(starAxisCount * angle);
				const auto star = std::exp(-(spokeOffset * spokeOffset) / (2.0f * starWidth * starWidth));
				auto glow = 0.5f / (std::max)(1.0f, falloff * edgeDist);
				glow *= (std::clamp)(1.0f - (taperGradient * (distance - maxSpriteRadius * taperZoneStart) / maxSpriteRadius), 0.0f, 1.0f);
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
	Pin<float> pinCornerRadius;

	static const int glowSize = 100;

public:
	LEDGui()
	{
		pinCornerRadius.value = 100.0f;
		pinAnimationPosition.onUpdate = [this](PinBase*) { onSetAnimationPosition(); };
		pinRed.onUpdate = [this](PinBase*) { onSetColor(); };
		pinGreen.onUpdate = [this](PinBase*) { onSetColor(); };
		pinBlue.onUpdate = [this](PinBase*) { onSetColor(); };
		pinCornerRadius.onUpdate = [this](PinBase*) { onSetCornerRadius(); };
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

			const auto brightness = std::clamp(pinAnimationPosition.value, 0.0f, 1.0f);
			const auto unlit = getLedColor(0.307f);
			const auto cornerRadius = getClampedCornerRadius();
			const RoundedRect shape{ bounds, cornerRadius, cornerRadius };

			auto fill = g.createSolidColorBrush(interpolateColor(unlit, Colors::White, brightness));
			g.fillRoundedRectangle(shape, fill);

			auto stroke = g.createSolidColorBrush(Color{ 0.3f, 0.3f, 0.3f, 1.0f });
			g.drawRoundedRectangle(shape, stroke);

			return ReturnCode::Ok;
		}

		if(layer != 1)
			return ReturnCode::NoSupport;

		const auto brightness = std::clamp(pinAnimationPosition.value, 0.0f, 1.0f);
		if(brightness <= 0.0f)
			return ReturnCode::Ok;

		Graphics g(drawingContext);

		const auto ledWidth = (std::max)(1.0f, getWidth(bounds));
		const auto ledHeight = (std::max)(1.0f, getHeight(bounds));
		const auto cornerRadius = getClampedCornerRadius();
		const auto bitmapWidth = 2 * glowSize + static_cast<int32_t>(std::ceil(ledWidth));
		const auto bitmapHeight = 2 * glowSize + static_cast<int32_t>(std::ceil(ledHeight));
		const auto glowRect = getGlowRect();

		auto rc = updateGlowBitmap(g, bitmapWidth, bitmapHeight, ledWidth, ledHeight, cornerRadius);
		if(rc != ReturnCode::Ok)
			return rc;

		const Rect srcRect{ 0.0f, 0.0f, static_cast<float>(bitmapWidth), static_cast<float>(bitmapHeight) };
		g.drawBitmap(glowBitmap, glowRect, srcRect, brightness);

		return ReturnCode::Ok;
	}

	ReturnCode hitTest(gmpi::drawing::Point point, int32_t flags) override
	{
		const auto center = getCenter(bounds);
		const auto halfW = 0.5f * getWidth(bounds);
		const auto halfH = 0.5f * getHeight(bounds);
		const auto cornerRadius = getClampedCornerRadius();
		const auto edgeDist = roundedRectSdf(point.x - center.x, point.y - center.y, halfW, halfH, cornerRadius);

		return edgeDist <= 0.0f ? ReturnCode::Ok : ReturnCode::Unhandled;
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
        <Pin name="Red" datatype="bool" default="1"/>
		<Pin name="Green" datatype="bool"/>
		<Pin name="Blue" datatype="bool"/>
		<Pin name="Corner Radius" datatype="float" default="100.0"/>
    </GUI>
</Plugin>
)XML");
}

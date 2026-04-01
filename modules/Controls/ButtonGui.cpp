// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.

#include "helpers/GmpiPluginEditor.h"
#include "helpers/CachedBlur.h"
#include <algorithm>
#include <cmath>

using namespace gmpi;
using namespace gmpi::drawing;
using namespace gmpi::editor;

namespace
{
Color withAlpha(Color color, float alpha)
{
	color.a = alpha;
	return color;
}
}

class ButtonGui final : public PluginEditor, public gmpi::api::IDrawingLayer
{
	Pin<bool> pinValue;
	Pin<std::string> pinHint;
	Pin<bool> pinMouseDown;
	Pin<bool> pinToggle;
	Pin<std::string> pinColor1;
	Pin<std::string> pinColor2;
	cachedBlur shadowBlur;
	SizeU shadowSize{};

	void redraw()
	{
		if(drawingHost)
		{
			Rect clipArea;
			getClipArea(&clipArea);
			drawingHost->invalidateRect(&clipArea);
		}
	}

	bool isPressed() const
	{
		return pinValue.value;
	}

	Color getFillColor() const
	{
		return pinColor1.value.empty() ? colorFromHex(0x2E79C7u) : colorFromHexString(pinColor1.value);
	}

	Color getHighlightColor() const
	{
		return pinColor2.value.empty() ? Color{ 1.0f, 1.0f, 1.0f, 0.94f } : colorFromHexString(pinColor2.value);
	}

	bool hasCapture() const
	{
		bool captured = false;
		if(inputHost.get())
		{
			inputHost->getCapture(captured);
		}
		return captured;
	}

	void updateShadowCache(const SizeU& size)
	{
		if(shadowSize.width != size.width || shadowSize.height != size.height)
		{
			shadowBlur.invalidate();
			shadowSize = size;
		}
	}

	Rect getLocalBounds(const SizeU& size) const
	{
		return { 0.0f, 0.0f, static_cast<float>(size.width), static_cast<float>(size.height) };
	}

	float getShadowExtent() const
	{
		const float minDimension = (std::min)(getWidth(bounds), getHeight(bounds));
		const float blurRadius = static_cast<float>((std::max)(1, static_cast<int>(std::ceil(minDimension * 0.08f))));
		const float blurOffset = 0.16f * minDimension;
		return blurOffset + blurRadius;
	}

	ReturnCode drawShadow(Graphics& g, const Rect& localBounds)
	{
		const auto width = getWidth(localBounds);
		const auto height = getHeight(localBounds);
		const float minDimension = (std::min)(width, height);
		const float cornerRadius = (std::max)(2.0f, minDimension * 0.24f);
		const float blurOffset = 0.16f * minDimension;
		const bool pressed = isPressed();
		const float shadowDirection = pressed ? 0.0f : 1.0f;
		const RoundedRect buttonRect(localBounds, cornerRadius);

		shadowBlur.tint = Color{ 0.0f, 0.0f, 0.0f, 0.6f };
		shadowBlur.blurRadius = (std::max)(1, static_cast<int>(std::ceil(minDimension * 0.08f)));

		const auto orig = g.getTransform();
		g.setTransform(makeTranslation({ shadowDirection * blurOffset, shadowDirection * blurOffset }) * orig);
		shadowBlur.draw(g, localBounds, [&](Graphics& mask)
			{
				auto brush = mask.createSolidColorBrush(Colors::White);
				mask.fillRoundedRectangle(buttonRect, brush);
			});
		g.setTransform(orig);

		return ReturnCode::Ok;
	}

	ReturnCode drawButton(Graphics& g, const Rect& localBounds)
	{
		const auto width = getWidth(localBounds);
		const auto height = getHeight(localBounds);
		const float minDimension = (std::min)(width, height);
		const float bevelWidth = (std::max)(1.0f, minDimension * 0.08f);
		const float cornerRadius = (std::max)(2.0f, minDimension * 0.24f);
		const bool pressed = isPressed();
		const Rect bodyRect = localBounds;
		const RoundedRect buttonRect(bodyRect, cornerRadius);

		// Fill
		{
			const auto fillColor = getFillColor();
			auto fillBrush = g.createRadialGradientBrush(
				{ bodyRect.left, bodyRect.top },
				(std::max)(width, height) * 1.8f,
				interpolateColor(fillColor, Colors::White, pressed ? 0.28f : 0.45f),
				interpolateColor(fillColor, Colors::Black, pressed ? 0.18f : 0.10f)
			);
			g.fillRoundedRectangle(buttonRect, fillBrush);
		}

		// Highlight
		{
			Rect highlightRect = bodyRect;
			highlightRect.left += bevelWidth * 1.2f;
			highlightRect.top += bevelWidth * 1.2f;
			highlightRect.right -= bevelWidth * 1.2f;
			highlightRect.bottom = highlightRect.top + height * 0.42f;
			const float highlightHeight = getHeight(highlightRect);
			if(getWidth(highlightRect) > bevelWidth && highlightHeight > bevelWidth)
			{
				const auto highlightColor = getHighlightColor();
				const RoundedRect highlightRounded(highlightRect, (std::max)(1.0f, cornerRadius - bevelWidth));
				auto highlightBrush = g.createLinearGradientBrush(
					{ highlightRect.left, highlightRect.top },
					{ highlightRect.left, highlightRect.bottom },
					withAlpha(interpolateColor(highlightColor, Colors::White, 0.25f), pressed ? 0.18f : 0.32f),
					withAlpha(highlightColor, 0.0f)
				);
				g.fillRoundedRectangle(highlightRounded, highlightBrush);
			}
		}

		// Bevel ring around edge
		{
			auto bevelBrush = g.createLinearGradientBrush(
				{ bodyRect.left, bodyRect.top },
				{ bodyRect.right, bodyRect.bottom },
				Color{ 0.85f, 0.85f, 0.85f, 0.55f },
				Color{ 0.20f, 0.20f, 0.20f, 0.55f }
			);
			g.drawRoundedRectangle(buttonRect, bevelBrush, bevelWidth);
		}

		return ReturnCode::Ok;
	}

public:
	ButtonGui()
	{
		pinValue.onUpdate = [this](PinBase*) { redraw(); };
		pinColor1.onUpdate = [this](PinBase*) { redraw(); };
		pinColor2.onUpdate = [this](PinBase*) { redraw(); };
	}

	ReturnCode getClipArea(Rect* returnRect) override
	{
		*returnRect = bounds;
		const auto shadowExtent = getShadowExtent();
		returnRect->left -= shadowExtent;
		returnRect->top -= shadowExtent;
		returnRect->right += shadowExtent;
		returnRect->bottom += shadowExtent;
		return ReturnCode::Ok;
	}

	int32_t addRef() override
	{
		return PluginEditor::addRef();
	}

	int32_t release() override
	{
		return PluginEditor::release();
	}

	ReturnCode renderLayer(drawing::api::IDeviceContext* drawingContext, int32_t layer) override
	{
		if (layer == -1)
		{
			Graphics g(drawingContext);
			const auto width = (std::max)(1u, static_cast<uint32_t>(getWidth(bounds)));
			const auto height = (std::max)(1u, static_cast<uint32_t>(getHeight(bounds)));
			const SizeU size{ width, height };
			updateShadowCache(size);
			return drawShadow(g, getLocalBounds(size));
		}

		if (layer == 0)
		{
			Graphics g(drawingContext);
			const auto width = (std::max)(1u, static_cast<uint32_t>(getWidth(bounds)));
			const auto height = (std::max)(1u, static_cast<uint32_t>(getHeight(bounds)));
			const SizeU size{ width, height };
			return drawButton(g, getLocalBounds(size));
		}

		return ReturnCode::NoSupport;
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

	ReturnCode onPointerDown(Point, int32_t flags) override
	{
		if((flags & static_cast<int32_t>(gmpi::api::GG_POINTER_FLAG_FIRSTBUTTON)) == 0)
			return ReturnCode::Ok;

		if(inputHost.get())
			inputHost->setCapture();

		pinMouseDown = true;
		pinValue = pinToggle.value ? !pinValue.value : true;

//		redraw();
		return ReturnCode::Ok;
	}

	ReturnCode onPointerMove(Point, int32_t) override
	{
		if(!hasCapture())
			return ReturnCode::Unhandled;

		return ReturnCode::Ok;
	}

	ReturnCode onPointerUp(Point, int32_t) override
	{
		if(!hasCapture())
			return ReturnCode::Unhandled;

		assert(inputHost.get());
		inputHost->releaseCapture();

		pinMouseDown = false;
		pinValue = pinToggle.value ? pinValue.value: false;

//		redraw();
		return ReturnCode::Ok;
	}
};

namespace
{
auto r = gmpi::Register<ButtonGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Button" name="Button" category="Sub-Controls">
    <GUI>
        <Pin name="Value" datatype="bool"/>
		<Pin name="Hint" datatype="string"/>
		<Pin name="Mouse Down" datatype="bool" direction="out"/>
		<Pin name="Toggle" datatype="bool"/>
		<Pin name="Color 1" datatype="string" default="2E79C7"/>
		<Pin name="Color 2" datatype="string" default="FFFFFFFF"/>
    </GUI>
</Plugin>
)XML");
}

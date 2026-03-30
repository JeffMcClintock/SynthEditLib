// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.

#include "helpers/GmpiPluginEditor.h"
#include "helpers/BitmapMask.h"
#include "helpers/CachedBlur.h"

#include <algorithm>
#include <cmath>

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::drawing;

class BumpGui final : public PluginEditor
{
   Bitmap shadowBitmap;
	SizeU shadowBitmapSize{};
	bool shadowBitmapDirty = true;

	Pin<bool> pinDip;
	Pin<bool> pinInnerShadow;
	Pin<bool> pinOuterShadow;
	Pin<bool> pinInnerHighlight;
	Pin<bool> pinOuterHighlight;
	Pin<float> pinBlackBlurAlpha;
	Pin<float> pinWhiteBlurAlpha;
	Pin<float> pinShadowDepth;
	Pin<float> pinCornerRadius;

	struct BlurSelection
	{
		bool innerShadow;
		bool outerShadow;
		bool innerHighlight;
		bool outerHighlight;
	};

	static constexpr int32_t kMask = (int32_t)BitmapRenderTargetFlags::Mask
		| (int32_t)BitmapRenderTargetFlags::CpuReadable;
	static constexpr int32_t kColorCpu = (int32_t)BitmapRenderTargetFlags::CpuReadable;
	static constexpr float kInsetScale = 24.0f / 128.0f;

	void redraw()
	{
		if(drawingHost)
			drawingHost->invalidateRect(&bounds);
	}

	void invalidateShadowBitmap()
	{
		shadowBitmapDirty = true;
	}

	Rect getLocalBounds(const SizeU& size) const
	{
		return { 0.0f, 0.0f, static_cast<float>(size.width), static_cast<float>(size.height) };
	}

	RoundedRect getShape(const Rect& localBounds) const
	{
		const auto width = getWidth(localBounds);
		const auto height = getHeight(localBounds);
		const auto inset = (std::min)(width, height) * kInsetScale;
		const auto insetX = (std::min)((std::max)(4.0f, inset), width * 0.25f);
		const auto insetY = (std::min)((std::max)(4.0f, inset), height * 0.25f);

		RoundedRect shape{ { insetX, insetY, width - insetX, height - insetY }, 0.0f, 0.0f };
		const auto maxRadius = 0.5f * (std::min)(getWidth(shape.rect), getHeight(shape.rect));
		const auto radius = (std::clamp)(pinCornerRadius.value, 0.0f, maxRadius);
		shape.radiusX = radius;
		shape.radiusY = radius;
		return shape;
	}

	float getBlackBlurAlpha() const
	{
		return (std::clamp)(pinBlackBlurAlpha.value, 0.0f, 1.0f);
	}

	float getWhiteBlurAlpha() const
	{
		return (std::clamp)(pinWhiteBlurAlpha.value, 0.0f, 1.0f);
	}

	float getShadowDepth() const
	{
		return (std::clamp)(pinShadowDepth.value, 0.0f, 1000.0f);
	}

	BlurSelection getBlurSelection() const
	{
		BlurSelection selection
		{
			pinInnerShadow.value,
			pinOuterShadow.value,
			pinInnerHighlight.value,
			pinOuterHighlight.value,
		};

		const auto usesExplicitSelection = selection.innerShadow || selection.innerHighlight || !selection.outerShadow || !selection.outerHighlight;
		if(!usesExplicitSelection && pinDip.value)
		{
			selection.innerShadow = true;
			selection.outerShadow = false;
			selection.innerHighlight = true;
			selection.outerHighlight = false;
		}

		return selection;
	}

	PathGeometry createHoleGeometry(Graphics& g, const Rect& localBounds, const RoundedRect& shape, const Size& offset) const
	{
		auto geom = g.getFactory().createPathGeometry();
		auto sink = geom.open();
		sink.setFillMode(FillMode::Alternate);
		sink.addRect(localBounds, FigureBegin::Filled);
		RoundedRect shifted = shape;
		shifted.rect = offsetRect(shifted.rect, offset);
		sink.addRoundedRect(shifted);
		sink.close();
		return geom;
	}

	Bitmap createMask(Graphics& g, const SizeU& size, const Rect& localBounds, const RoundedRect& shape, bool inner) const
	{
		auto maskRT = g.getFactory().createCpuRenderTarget(size, kMask);
		maskRT.beginDraw();
		maskRT.clear(Color{ 0.0f, 0.0f, 0.0f, 0.0f });
		{
			auto whiteBrush = maskRT.createSolidColorBrush(Colors::White);
			if(inner)
			{
				maskRT.fillRoundedRectangle(shape, whiteBrush);
			}
			else
			{
				auto geom = createHoleGeometry(maskRT, localBounds, shape, Size{ 0.0f, 0.0f });
				maskRT.fillGeometry(geom, whiteBrush);
			}
		}
		maskRT.endDraw();
		return maskRT.getBitmap();
	}

	void addBlurPass(Graphics& target, const SizeU& size, const Rect& localBounds, Bitmap mask, const Color& tint, std::function<void(Graphics&)> drawer) const
	{
		auto blurRT = target.getFactory().createCpuRenderTarget(size, kColorCpu);
		blurRT.beginDraw();
		blurRT.clear(Color{ 0.0f, 0.0f, 0.0f, 0.0f });
		{
			cachedBlur blur;
			blur.tint = tint;
			blur.draw(blurRT, localBounds, drawer);
		}
		blurRT.endDraw();

		auto blurBmp = blurRT.getBitmap();
		applyMask(blurBmp, mask);
		target.drawBitmap(blurBmp, localBounds, localBounds);
	}

	Bitmap renderBlurs(Graphics& g, const SizeU& size, const Rect& localBounds, const RoundedRect& shape) const
	{
		auto factory = g.getFactory();
		auto shadowRT = factory.createCpuRenderTarget(size, kColorCpu);
		shadowRT.beginDraw();
		shadowRT.clear(Color{ 0.0f, 0.0f, 0.0f, 0.0f });
		{
			auto selection = getBlurSelection();
			const auto blackBlurAlpha = getBlackBlurAlpha();
			const auto whiteBlurAlpha = getWhiteBlurAlpha();
			const auto shadowDepth = getShadowDepth();
			auto innerMask = createMask(g, size, localBounds, shape, true);
			auto outerMask = createMask(g, size, localBounds, shape, false);

			if(selection.innerShadow)
			{
				addBlurPass(shadowRT, size, localBounds, innerMask, Color{ 0.0f, 0.0f, 0.0f, blackBlurAlpha }, [&](Graphics& m)
					{
						auto geom = createHoleGeometry(m, localBounds, shape, Size{ shadowDepth, shadowDepth });
						auto brush = m.createSolidColorBrush(Colors::White);
						m.fillGeometry(geom, brush);
					});
			}

			if(selection.innerHighlight)
			{
				addBlurPass(shadowRT, size, localBounds, innerMask, Color{ 1.0f, 1.0f, 1.0f, whiteBlurAlpha }, [&](Graphics& m)
					{
						auto geom = createHoleGeometry(m, localBounds, shape, Size{ -shadowDepth, -shadowDepth });
						auto brush = m.createSolidColorBrush(Colors::White);
						m.fillGeometry(geom, brush);
					});
			}

			if(selection.outerHighlight)
			{
				addBlurPass(shadowRT, size, localBounds, outerMask, Color{ 1.0f, 1.0f, 1.0f, whiteBlurAlpha }, [&](Graphics& m)
					{
						auto brush = m.createSolidColorBrush(Colors::White);
						RoundedRect shifted = shape;
						shifted.rect = offsetRect(shifted.rect, Size{ -shadowDepth, -shadowDepth });
						m.fillRoundedRectangle(shifted, brush);
					});
			}

			if(selection.outerShadow)
			{
				addBlurPass(shadowRT, size, localBounds, outerMask, Color{ 0.0f, 0.0f, 0.0f, blackBlurAlpha }, [&](Graphics& m)
					{
						auto brush = m.createSolidColorBrush(Colors::White);
						RoundedRect shifted = shape;
						shifted.rect = offsetRect(shifted.rect, Size{ shadowDepth, shadowDepth });
						m.fillRoundedRectangle(shifted, brush);
					});
			}
		}
		shadowRT.endDraw();
		return shadowRT.getBitmap();
	}

	ReturnCode updateShadowBitmap(Graphics& g, const SizeU& size, const Rect& localBounds, const RoundedRect& shape)
	{
		if(!shadowBitmapDirty && shadowBitmap && shadowBitmapSize == size)
			return ReturnCode::Ok;

		shadowBitmap = renderBlurs(g, size, localBounds, shape);
		shadowBitmapSize = size;
		shadowBitmapDirty = false;
		return ReturnCode::Ok;
	}

public:
	BumpGui()
	{
		pinDip.value = false;
		pinInnerShadow.value = false;
		pinOuterShadow.value = true;
		pinInnerHighlight.value = false;
		pinOuterHighlight.value = true;
		pinBlackBlurAlpha.value = 0.5f;
		pinWhiteBlurAlpha.value = 0.5f;
		pinShadowDepth.value = 4.0f;
		pinCornerRadius.value = 16.0f;
       pinDip.onUpdate = [this](PinBase*) { invalidateShadowBitmap(); redraw(); };
		pinInnerShadow.onUpdate = [this](PinBase*) { invalidateShadowBitmap(); redraw(); };
		pinOuterShadow.onUpdate = [this](PinBase*) { invalidateShadowBitmap(); redraw(); };
		pinInnerHighlight.onUpdate = [this](PinBase*) { invalidateShadowBitmap(); redraw(); };
		pinOuterHighlight.onUpdate = [this](PinBase*) { invalidateShadowBitmap(); redraw(); };
		pinBlackBlurAlpha.onUpdate = [this](PinBase*) { invalidateShadowBitmap(); redraw(); };
		pinWhiteBlurAlpha.onUpdate = [this](PinBase*) { invalidateShadowBitmap(); redraw(); };
		pinShadowDepth.onUpdate = [this](PinBase*) { invalidateShadowBitmap(); redraw(); };
		pinCornerRadius.onUpdate = [this](PinBase*) { invalidateShadowBitmap(); redraw(); };
	}

	ReturnCode render(drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);
		const auto width = (std::max)(1.0f, getWidth(bounds));
		const auto height = (std::max)(1.0f, getHeight(bounds));
		const SizeU size{ static_cast<uint32_t>(std::ceil(width)), static_cast<uint32_t>(std::ceil(height)) };
		const auto localBounds = getLocalBounds(size);
		const auto shape = getShape(localBounds);

      auto rc = updateShadowBitmap(g, size, localBounds, shape);
		if(rc != ReturnCode::Ok)
			return rc;

		g.drawBitmap(shadowBitmap, bounds, localBounds);
		return ReturnCode::Ok;
	}
};

namespace
{
auto r = gmpi::Register<BumpGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Bump" name="Bump" category="Sub-Controls">
    <GUI graphicsApi="GmpiGui">
        <Pin name="Dip" datatype="bool"/>
        <Pin name="Inner Shadow" datatype="bool"/>
		<Pin name="Outer Shadow" datatype="bool"/>
		<Pin name="Inner Highlight" datatype="bool"/>
		<Pin name="Outer Highlight" datatype="bool"/>
        <Pin name="Black Blur Alpha" datatype="float" default="0.5"/>
		<Pin name="White Blur Alpha" datatype="float" default="0.3"/>
		<Pin name="Shadow Depth" datatype="float" default="4.0"/>
        <Pin name="Corner Radius" datatype="float" default="16.0"/>
    </GUI>
</Plugin>
)XML");
}

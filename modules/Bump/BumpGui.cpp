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
#include "helpers/CachedBlur.h"

#include <algorithm>
#include <cmath>

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::drawing;

class BumpGui final : public PluginEditor
{
	Pin<bool> pinDip;
	Pin<float> pinCornerRadius;

	static constexpr int32_t kMask = (int32_t)BitmapRenderTargetFlags::Mask
		| (int32_t)BitmapRenderTargetFlags::CpuReadable;
	static constexpr int32_t kColorCpu = (int32_t)BitmapRenderTargetFlags::CpuReadable;
	static constexpr float kInsetScale = 24.0f / 128.0f;
	static constexpr float kShadowOffset = 4.0f;

	void redraw()
	{
		if (drawingHost)
			drawingHost->invalidateRect(&bounds);
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

	Bitmap renderDip(Graphics& g, const SizeU& size, const Rect& localBounds, const RoundedRect& shape) const
	{
		auto factory = g.getFactory();
		auto shadowRT = factory.createCpuRenderTarget(size, kColorCpu);
		shadowRT.beginDraw();
		shadowRT.clear(Color{ 0.0f, 0.0f, 0.0f, 0.0f });
		{
			auto makeHoleGeom = [&](Graphics& ctx, float dx, float dy)
			{
				auto geom = ctx.getFactory().createPathGeometry();
				auto sink = geom.open();
				sink.setFillMode(FillMode::Alternate);
				sink.addRect(localBounds, FigureBegin::Filled);
				sink.addRoundedRect({ { shape.rect.left + dx, shape.rect.top + dy, shape.rect.right + dx, shape.rect.bottom + dy }, shape.radiusX, shape.radiusY });
				sink.close();
				return geom;
			};

			cachedBlur innerDark;
			innerDark.tint = Color{ 0.0f, 0.0f, 0.0f, 0.5f };
			innerDark.draw(shadowRT, localBounds, [&](Graphics& m)
			{
				auto darkGeom = makeHoleGeom(m, kShadowOffset, kShadowOffset);
				auto brush = m.createSolidColorBrush(Colors::White);
				m.fillGeometry(darkGeom, brush);
			});

			cachedBlur innerLight;
			innerLight.tint = Color{ 1.0f, 1.0f, 1.0f, 0.8f };
			innerLight.draw(shadowRT, localBounds, [&](Graphics& m)
			{
				auto lightGeom = makeHoleGeom(m, -kShadowOffset, -kShadowOffset);
				auto brush = m.createSolidColorBrush(Colors::White);
				m.fillGeometry(lightGeom, brush);
			});
		}
		shadowRT.endDraw();

		auto maskRT = factory.createCpuRenderTarget(size, kMask);
		maskRT.beginDraw();
		maskRT.clear(Color{ 0.0f, 0.0f, 0.0f, 0.0f });
		{
			auto whiteBrush = maskRT.createSolidColorBrush(Colors::White);
			maskRT.fillRoundedRectangle(shape, whiteBrush);
		}
		maskRT.endDraw();

		auto shadowBmp = shadowRT.getBitmap();
		auto maskBmp = maskRT.getBitmap();
		applyMask(shadowBmp, maskBmp);
		return shadowBmp;
	}

	Bitmap renderBump(Graphics& g, const SizeU& size, const Rect& localBounds, const RoundedRect& shape) const
	{
		auto factory = g.getFactory();
		auto shadowRT = factory.createCpuRenderTarget(size, kColorCpu);
		shadowRT.beginDraw();
		shadowRT.clear(Color{ 0.0f, 0.0f, 0.0f, 0.0f });
		{
			cachedBlur lightShadow;
			lightShadow.tint = Color{ 1.0f, 1.0f, 1.0f, 0.7f };
			lightShadow.draw(shadowRT, localBounds, [&](Graphics& m)
			{
				auto brush = m.createSolidColorBrush(Colors::White);
				RoundedRect shifted = shape;
				shifted.rect = offsetRect(shifted.rect, Size{ -kShadowOffset, -kShadowOffset });
				m.fillRoundedRectangle(shifted, brush);
			});

			cachedBlur darkShadow;
			darkShadow.tint = Color{ 0.0f, 0.0f, 0.0f, 0.5f };
			darkShadow.draw(shadowRT, localBounds, [&](Graphics& m)
			{
				auto brush = m.createSolidColorBrush(Colors::White);
				RoundedRect shifted = shape;
				shifted.rect = offsetRect(shifted.rect, Size{ kShadowOffset, kShadowOffset });
				m.fillRoundedRectangle(shifted, brush);
			});
		}
		shadowRT.endDraw();

		auto maskRT = factory.createCpuRenderTarget(size, kMask);
		maskRT.beginDraw();
		maskRT.clear(Color{ 0.0f, 0.0f, 0.0f, 0.0f });
		{
			auto geom = maskRT.getFactory().createPathGeometry();
			auto sink = geom.open();
			sink.setFillMode(FillMode::Alternate);
			sink.addRect(localBounds, FigureBegin::Filled);
			sink.addRoundedRect(shape);
			sink.close();
			auto whiteBrush = maskRT.createSolidColorBrush(Colors::White);
			maskRT.fillGeometry(geom, whiteBrush);
		}
		maskRT.endDraw();

		auto shadowBmp = shadowRT.getBitmap();
		auto maskBmp = maskRT.getBitmap();
		applyMask(shadowBmp, maskBmp);
		return shadowBmp;
	}

public:
	BumpGui()
	{
		pinDip.value = false;
		pinCornerRadius.value = 16.0f;
		pinDip.onUpdate = [this](PinBase*) { redraw(); };
		pinCornerRadius.onUpdate = [this](PinBase*) { redraw(); };
	}

	ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);
		const auto width = (std::max)(1.0f, getWidth(bounds));
		const auto height = (std::max)(1.0f, getHeight(bounds));
		const SizeU size{ static_cast<uint32_t>(std::ceil(width)), static_cast<uint32_t>(std::ceil(height)) };
		const auto localBounds = getLocalBounds(size);
		const auto shape = getShape(localBounds);

		auto shadowBmp = pinDip.value ? renderDip(g, size, localBounds, shape) : renderBump(g, size, localBounds, shape);
		g.drawBitmap(shadowBmp, bounds, localBounds);
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
        <Pin name="Corner Radius" datatype="float"/>
    </GUI>
</Plugin>
)XML");
}

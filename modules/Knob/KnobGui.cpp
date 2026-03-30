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
#include "helpers/CachedBlur.h"

#include <algorithm>
#include <cmath>

using namespace gmpi;
using namespace gmpi::editor;
using namespace gmpi::drawing;

class KnobGui final : public PluginEditor
{
	cachedBlur shadowBlur;
	SizeU cachedShadowSize{};
	Pin<float> pinValue;

	void redraw()
	{
		if(drawingHost)
			drawingHost->invalidateRect(&bounds);
	}

	float getNormalizedValue() const
	{
		return (std::clamp)(pinValue.value, 0.0f, 1.0f);
	}

	Rect getLocalBounds(const SizeU& size) const
	{
		return { 0.0f, 0.0f, static_cast<float>(size.width), static_cast<float>(size.height) };
	}

	RoundedRect makeCircleRect(float centerX, float centerY, float radius) const
	{
		RoundedRect shape;
		shape.rect.left = centerX - radius;
		shape.rect.top = centerY - radius;
		shape.rect.right = centerX + radius;
		shape.rect.bottom = centerY + radius;
		shape.radiusX = radius;
		shape.radiusY = radius;
		return shape;
	}

	void invalidateShadowCache(const SizeU& size)
	{
		if(cachedShadowSize.width != size.width || cachedShadowSize.height != size.height)
		{
			shadowBlur.invalidate();
			cachedShadowSize = size;
		}
	}

	ReturnCode drawKnob(Graphics& target, const Rect& localBounds)
	{
       auto center = getCenter(localBounds);
		const float maxRadius = 0.5f * (std::min)(getWidth(localBounds), getHeight(localBounds));
		const float margin = (std::max)(4.0f, maxRadius * 0.24f);
		const float outerRadius = (std::max)(1.0f, maxRadius - margin);
		const float bodyRadius = outerRadius * 0.88f;
		const float coreRadius = outerRadius * 0.68f;
		const float normalized = getNormalizedValue();
		const RoundedRect rimShape = makeCircleRect(center.x, center.y, outerRadius);
		const RoundedRect bezelShape = makeCircleRect(center.x, center.y, bodyRadius);
		const RoundedRect coreShape = makeCircleRect(center.x, center.y, coreRadius);

		target.clear(Color{ 0.0f, 0.0f, 0.0f, 0.0f });

		shadowBlur.tint = Color{ 0.0f, 0.0f, 0.0f, 0.28f };
		shadowBlur.draw(target, localBounds, [&](Graphics& mask)
			{
				auto brush = mask.createSolidColorBrush(Colors::White);
                auto shadowCenter = center;
				shadowCenter.x += outerRadius * 0.14f;
				shadowCenter.y += outerRadius * 0.16f;
				auto shadowShape = makeCircleRect(shadowCenter.x, shadowCenter.y, outerRadius * 0.86f);
				mask.fillRoundedRectangle(shadowShape, brush);
			});

		auto rimBrush = target.createRadialGradientBrush(center, outerRadius, colorFromHex(0x6F7783u), colorFromHex(0x12151Bu));
       target.fillRoundedRectangle(rimShape, rimBrush);

		auto bezelBrush = target.createRadialGradientBrush(center, bodyRadius, colorFromHex(0x49515Du), colorFromHex(0x1E242Eu));
     target.fillRoundedRectangle(bezelShape, bezelBrush);

		auto centerBrush = target.createRadialGradientBrush(center, coreRadius, colorFromHex(0x8AC7FFu), colorFromHex(0x2E79C7u));
     target.fillRoundedRectangle(coreShape, centerBrush);

		auto glossBrush = target.createSolidColorBrush(Color{ 1.0f, 1.0f, 1.0f, 0.12f });
        auto glossCenter = center;
		glossCenter.x -= coreRadius * 0.24f;
		glossCenter.y -= coreRadius * 0.28f;
		target.fillRoundedRectangle(makeCircleRect(glossCenter.x, glossCenter.y, coreRadius * 0.46f), glossBrush);

		auto edgeBrush = target.createSolidColorBrush(Color{ 0.0f, 0.0f, 0.0f, 0.45f });
      target.drawRoundedRectangle(rimShape, edgeBrush, (std::max)(1.0f, outerRadius * 0.08f));

		auto innerEdgeBrush = target.createSolidColorBrush(Color{ 1.0f, 1.0f, 1.0f, 0.18f });
       target.drawRoundedRectangle(bezelShape, innerEdgeBrush, (std::max)(1.0f, outerRadius * 0.04f));

		constexpr float kPi = 3.14159265f;
     const float angle = (135.0f + 270.0f * normalized) * (kPi / 180.0f);
        auto direction = center;
		direction.x = std::cos(angle);
		direction.y = std::sin(angle);
		auto start = center;
		start.x += direction.x * coreRadius * 0.18f;
		start.y += direction.y * coreRadius * 0.18f;
		auto end = center;
		end.x += direction.x * coreRadius * 0.82f;
		end.y += direction.y * coreRadius * 0.82f;
		auto indicatorShadow = target.createSolidColorBrush(Color{ 0.0f, 0.0f, 0.0f, 0.25f });
      auto shadowStart = start;
		shadowStart.x += 1.0f;
		shadowStart.y += 1.0f;
		auto shadowEnd = end;
		shadowEnd.x += 1.0f;
		shadowEnd.y += 1.0f;
		target.drawLine(shadowStart, shadowEnd, indicatorShadow, (std::max)(2.0f, outerRadius * 0.10f));
		auto indicatorBrush = target.createSolidColorBrush(Color{ 1.0f, 1.0f, 1.0f, 0.94f });
		target.drawLine(start, end, indicatorBrush, (std::max)(2.0f, outerRadius * 0.09f));

		return ReturnCode::Ok;
	}

public:
	KnobGui()
	{
		pinValue.onUpdate = [this](PinBase*) { redraw(); };
	}

	ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);
		const auto width = (std::max)(1.0f, getWidth(bounds));
		const auto height = (std::max)(1.0f, getHeight(bounds));
		const SizeU size{ static_cast<uint32_t>(std::ceil(width)), static_cast<uint32_t>(std::ceil(height)) };
		const auto localBounds = getLocalBounds(size);

		invalidateShadowCache(size);

		auto offscreen = g.getFactory().createCpuRenderTarget(size, 0);
		offscreen.beginDraw();
		auto rc = drawKnob(offscreen, localBounds);
		offscreen.endDraw();
		if(rc != ReturnCode::Ok)
			return rc;

		g.drawBitmap(offscreen.getBitmap(), bounds, localBounds, 1.0f, BitmapInterpolationMode::Linear);
		return ReturnCode::Ok;
	}
};

namespace
{
auto r = gmpi::Register<KnobGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Knob" name="Knob" category="Sub-Controls">
    <GUI graphicsApi="GmpiGui">
        <Pin name="Value" datatype="float" default="0.0"/>
    </GUI>
</Plugin>
)XML");
}

// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.

#include "ControlsBase.h"

class HSliderGui final : public ValueControlBase, public gmpi::api::IDrawingLayer
{
	Rect previousDirtyRect{};

	Rect getHandleDirtyRect() const
	{
		const auto width = (std::max)(1u, static_cast<uint32_t>(getWidth(bounds)));
		const auto height = (std::max)(1u, static_cast<uint32_t>(getHeight(bounds)));
		auto dirty = getHandleAndShadowRect(getLocalBounds({ width, height }));
		dirty.left += bounds.left;
		dirty.top += bounds.top;
		dirty.right += bounds.left;
		dirty.bottom += bounds.top;
		return dirty;
	}

	void redraw()
	{
		assert(drawingHost.get());

		auto dirty = getHandleDirtyRect();
		auto combined = Rect{
			(std::min)(dirty.left, previousDirtyRect.left),
			(std::min)(dirty.top, previousDirtyRect.top),
			(std::max)(dirty.right, previousDirtyRect.right),
			(std::max)(dirty.bottom, previousDirtyRect.bottom)
		};
		previousDirtyRect = dirty;
		drawingHost->invalidateRect(&combined);
	}

	Rect getHandleAndShadowRect(const Rect& localBounds) const
	{
		const auto handleRect = getHandleRect(localBounds);
		const float radius = (std::max)(1.0f, getWidth(handleRect) * 0.5f);
		const float blurRadius = static_cast<float>((std::max)(1, static_cast<int>(std::ceil(radius * 0.1f))));
		const float blurOffset = 0.4f * radius;
		return {
			handleRect.left,
			handleRect.top,
			handleRect.right + blurOffset + blurRadius,
			handleRect.bottom + blurOffset + blurRadius
		};
	}

	// get the handle rectangle at a given normalized value
	Rect getHandleRect(const Rect& localBounds) const
	{
		const float width = getWidth(localBounds);
		const float height = getHeight(localBounds);
		const float handleWidth = (std::max)(12.0f, height * 0.5f);
		const float margin = handleWidth * 0.5f;
		const float trackLeft = localBounds.left + margin;
		const float trackRight = localBounds.right - margin;
		const float trackRange = trackRight - trackLeft;
		const float normalized = getNormalizedValue();

		// value 0.0 = left, value 1.0 = right
		const float handleCenterX = trackLeft + normalized * trackRange;

		return {
			handleCenterX - handleWidth * 0.5f,
			localBounds.top,
			handleCenterX + handleWidth * 0.5f,
			localBounds.bottom
		};
	}

	float valueFromPoint(const Rect& localBounds, Point point) const
	{
		const float width = getWidth(localBounds);
		const float handleWidth = (std::max)(12.0f, width * 0.12f);
		const float margin = handleWidth * 0.5f;
		const float trackLeft = localBounds.left + margin;
		const float trackRight = localBounds.right - margin;
		const float trackRange = trackRight - trackLeft;

		if(trackRange <= 0.0f)
			return 0.0f;

		// left = 0.0, right = 1.0
		return (std::clamp)((point.x - trackLeft) / trackRange, 0.0f, 1.0f);
	}

	ReturnCode drawShadow(Graphics& g, const Rect& localBounds)
	{
		const float height = getHeight(localBounds);

		// Track background (horizontal)
		{
			const float trackStrokeWidth = (std::max)(4.0f, height * 0.15f);
			const float trackTop = localBounds.top + (height - trackStrokeWidth) * 0.5f;
			const float trackRadius = trackStrokeWidth * 0.5f;

			auto trackBrush = g.createSolidColorBrush(Color{ 0.0f, 0.0f, 0.0f, 0.15f });
			g.fillRoundedRectangle(
				{ { localBounds.left, trackTop, localBounds.right, trackTop + trackStrokeWidth }, trackRadius, trackRadius },
				trackBrush
			);
		}

		const auto handleRect = getHandleRect(localBounds);
		const float radius = (std::max)(1.0f, getWidth(handleRect) * 0.5f);

		shadowBlur.tint = Color{ 0.0f, 0.0f, 0.0f, 0.6f };
		shadowBlur.blurRadius = (std::max)(1, static_cast<int>(std::ceil(radius * 0.1f)));
		const float blurOffset = 0.4f * radius;
		const float handleW = getWidth(handleRect);
		const float handleH = getHeight(handleRect);

		const auto orig = g.getTransform();
		g.setTransform(makeTranslation({ handleRect.left + blurOffset, blurOffset }) * orig);
		const float bitmapW = (std::max)(handleW, handleH);
		gmpi::drawing::Rect bitmapRect{ 0.0f, 0.0f, bitmapW, handleH };
		shadowBlur.draw(g, bitmapRect, [&](Graphics& mask)
			{
				Rect shapeRect{ 0.0f, 0.0f, handleW, handleH };
				auto shadowRect = inflateRect(shapeRect, -shadowBlur.blurRadius);
				auto brush = mask.createSolidColorBrush(Colors::White);
				mask.fillRoundedRectangle(
					{ shadowRect, radius, radius },
					brush
				);
			});
		g.setTransform(orig);

		return ReturnCode::Ok;
	}

	ReturnCode drawSlider(Graphics& g, const Rect& localBounds)
	{
		const float height = getHeight(localBounds);

		const auto handleRect = getHandleRect(localBounds);
		const float radius = (std::max)(1.0f, getWidth(handleRect) * 0.5f);
		const float bevelWidth = (std::max)(1.0f, radius * 0.12f);

		// Handle fill
		{
			const auto fillColor = getFillColor();
			auto fillBrush = g.createLinearGradientBrush(
				{ handleRect.left, handleRect.top },
				{ handleRect.right, handleRect.bottom },
				interpolateColor(fillColor, Colors::White, 0.35f),
				interpolateColor(fillColor, Colors::Black, 0.05f)
			);
			g.fillRoundedRectangle(
				{ handleRect, radius, radius },
				fillBrush
			);
		}

		// Handle indicator
		{
			const float centerX = (handleRect.left + handleRect.right) * 0.5f;
			const float ht = (std::min)((std::max)(1.0f, height * 0.05f), radius * 0.95f);
			const float d = std::sqrt(radius * radius - ht * ht);

			const float topY = handleRect.top + radius - d;
			const float bottomY = handleRect.bottom - radius + d;

			auto geometry = g.getFactory().createPathGeometry();
			auto sink = geometry.open();
			sink.beginFigure({ centerX - ht, topY }, FigureBegin::Filled);
			sink.addArc({ { centerX + ht, topY }, { radius, radius }, 0.f, SweepDirection::Clockwise, ArcSize::Small });
			sink.addLine({ centerX + ht, bottomY });
			sink.addArc({ { centerX - ht, bottomY }, { radius, radius }, 0.f, SweepDirection::Clockwise, ArcSize::Small });
			sink.endFigure();
			sink.close();

			auto indicatorBrush = g.createSolidColorBrush(getIndicatorColor());
			g.fillGeometry(geometry, indicatorBrush);
		}

		// Bevel ring around handle
		{
			const auto bevelRect = Rect{
				handleRect.left + bevelWidth * 0.5f,
				handleRect.top + bevelWidth * 0.5f,
				handleRect.right - bevelWidth * 0.5f,
				handleRect.bottom - bevelWidth * 0.5f
			};
			const float bevelCorner = (std::max)(0.0f, radius - bevelWidth * 0.5f);

			auto bevelBrush = g.createLinearGradientBrush(
				{ bevelRect.left, bevelRect.top },
				{ bevelRect.right, bevelRect.bottom },
				Color{ 0.85f, 0.85f, 0.85f, 0.55f },
				Color{ 0.20f, 0.20f, 0.20f, 0.55f }
			);
			g.drawRoundedRectangle(
				{ bevelRect, bevelCorner, bevelCorner },
				bevelBrush,
				bevelWidth
			);
		}

		return ReturnCode::Ok;
	}

public:
	HSliderGui() = default;

	ReturnCode getClipArea(Rect* returnRect) override
	{
		const auto width = (std::max)(1u, static_cast<uint32_t>(getWidth(bounds)));
		const auto height = (std::max)(1u, static_cast<uint32_t>(getHeight(bounds)));
		const auto localBounds = getLocalBounds({ width, height });
		auto shadowRect = getHandleAndShadowRect(localBounds);
		shadowRect.left += bounds.left;
		shadowRect.top += bounds.top;
		shadowRect.right += bounds.left;
		shadowRect.bottom += bounds.top;

		*returnRect = {
			(std::min)(bounds.left, shadowRect.left),
			(std::min)(bounds.top, shadowRect.top),
			(std::max)(bounds.right, shadowRect.right),
			(std::max)(bounds.bottom, shadowRect.bottom)
		};
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
			return drawSlider(g, getLocalBounds(size));
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

	ReturnCode onPointerDown(Point point, int32_t flags) override
	{
		if((flags & static_cast<int32_t>(gmpi::api::GG_POINTER_FLAG_FIRSTBUTTON)) == 0)
			return ReturnCode::Ok;

		pointPrevious = point;
		pinMouseDown = true;

		if(inputHost.get())
			inputHost->setCapture();

		// jump to clicked position
		const auto width = (std::max)(1u, static_cast<uint32_t>(getWidth(bounds)));
		const auto height = (std::max)(1u, static_cast<uint32_t>(getHeight(bounds)));
		const auto localBounds = getLocalBounds({ width, height });
		const auto newValue = valueFromPoint(localBounds, point);
		pinValue = newValue;

		return ReturnCode::Ok;
	}

	ReturnCode onPointerMove(Point point, int32_t flags) override
	{
		if(!hasCapture())
			return ReturnCode::Unhandled;

		const auto width = (std::max)(1u, static_cast<uint32_t>(getWidth(bounds)));
		const auto height = (std::max)(1u, static_cast<uint32_t>(getHeight(bounds)));
		const auto localBounds = getLocalBounds({ width, height });

		const bool fineControl = (flags & static_cast<int32_t>(gmpi::api::GG_POINTER_KEY_CONTROL)) != 0;
		if(fineControl)
		{
			const float coarseness = 0.001f;
			auto newValue = pinValue.value + coarseness * (point.x - pointPrevious.x);
			newValue = (std::clamp)(newValue, 0.0f, 1.0f);
			pinValue = newValue;
		}
		else
		{
			const auto newValue = valueFromPoint(localBounds, point);
			pinValue = newValue;
		}

		pointPrevious = point;
		return ReturnCode::Ok;
	}

	ReturnCode onPointerUp(Point point, int32_t flags) override
	{
		if(!hasCapture())
			return ReturnCode::Unhandled;

		if(inputHost.get())
			inputHost->releaseCapture();

		pinMouseDown = false;
		return ReturnCode::Ok;
	}

};

namespace
{
auto r = gmpi::Register<HSliderGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE HSlider" name="HSlider" category="Sub-Controls">
    <GUI graphicsApi="GmpiGui">
        <Pin name="Normalized" datatype="float" default="0.0"/>
		<Pin name="Hint" datatype="string"/>
		<Pin name="Menu Items" datatype="string"/>
		<Pin name="Menu Selection" datatype="int" direction="out"/>
		<Pin name="Mouse Down" datatype="bool" direction="out"/>
		<Pin name="Color 1" datatype="string" default="2E79C7"/>
		<Pin name="Color 2" datatype="string" default="FFFFFFFF"/>
    </GUI>
</Plugin>
)XML");
}

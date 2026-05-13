// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.

#include "ControlsBase.h"

class ModWheelGui final : public ValueControlBase, public gmpi::api::IDrawingLayer
{
	struct Layout
	{
		Rect slot;
		Rect face;
		float slotCornerR;
		float faceCornerR;
		float wheelCenterY;
		float wheelRadius;   // half-height of the visible cylinder face
		float stripeTopY;    // foreshortened top edge of the position stripe
		float stripeBotY;    // foreshortened bottom edge of the position stripe
	};

	Layout computeLayout(const Rect& localBounds) const
	{
		const float width = getWidth(localBounds);

		// Slot inset — the recessed channel housing the wheel
		const float slotMarginX = (std::max)(1.5f, width * 0.18f);
		const float slotMarginY = (std::max)(1.5f, width * 0.16f);
		const Rect slot{
			localBounds.left + slotMarginX,
			localBounds.top + slotMarginY,
			localBounds.right - slotMarginX,
			localBounds.bottom - slotMarginY
		};
		const float slotCornerR = (std::max)(1.5f, getWidth(slot) * 0.42f);

		// Cylinder face inside the slot — small inset so the slot rim is visible
		const float faceInsetX = (std::max)(0.5f, getWidth(slot) * 0.06f);
		const float faceInsetY = (std::max)(0.5f, getWidth(slot) * 0.04f);
		const Rect face{
			slot.left + faceInsetX,
			slot.top + faceInsetY,
			slot.right - faceInsetX,
			slot.bottom - faceInsetY
		};
		const float faceCornerR = (std::max)(1.0f, slotCornerR - faceInsetX);

		const float wheelRadius = (face.bottom - face.top) * 0.5f;
		const float wheelCenterY = (face.top + face.bottom) * 0.5f;

		// Stripe is a horizontal line painted onto the cylinder surface. As the
		// cylinder rotates, both its centre AND its visible thickness foreshorten
		// via sin(): widest in the middle (facing us), narrowing at the extremes
		// where the surface tilts away. Range is slightly less than ±π/2 so the
		// stripe never sits exactly on the rounded face cap.
		constexpr float kPi = 3.14159265f;
		constexpr float kArcSpan = 0.85f * kPi;
		const float normalized = getNormalizedValue();
		const float angle = (normalized - 0.5f) * kArcSpan;

		// Derive the stripe's angular half-width from the desired on-axis pixel
		// thickness. asin() inverts the projection so the centre stripe matches
		// the intended pixel size exactly.
		const float stripeThickPx = (std::max)(2.0f, getWidth(face) * 0.20f);
		const float halfThickRatio = (std::min)(0.95f, (stripeThickPx * 0.5f) / (std::max)(1.0f, wheelRadius));
		const float halfAngle = std::asin(halfThickRatio);

		const float stripeTopY = wheelCenterY - wheelRadius * std::sin(angle + halfAngle);
		const float stripeBotY = wheelCenterY - wheelRadius * std::sin(angle - halfAngle);

		return {
			slot, face,
			slotCornerR, faceCornerR,
			wheelCenterY, wheelRadius,
			stripeTopY, stripeBotY
		};
	}

	int getShadowBlurRadius() const
	{
		const float width = (std::max)(1.f, getWidth(bounds));
		return 1; // (std::max)(2, static_cast<int>(std::ceil(width * 0.15f)));
	}

	ReturnCode drawShadow(Graphics& g, const Rect& localBounds)
	{
		const auto layout = computeLayout(localBounds);

		shadowBlur.tint = Color{ 0.0f, 0.0f, 0.0f, 0.55f };
		shadowBlur.blurRadius = getShadowBlurRadius();

		// The shadow is a single closed D-shape path:
		//   1. top-left rounded corner
		//   2. flat top edge (length = wheel face width — the dimension the
		//      cylinder actually casts)
		//   3. one elliptical arc from top-right to bottom-right (the cylinder's
		//      end-cap projection, bulging right)
		//   4. flat bottom edge
		//   5. bottom-left rounded corner
		//   6. left edge closing the path
		// The shape's top is tucked behind the wheel's rounded top corner by
		// slotCornerR; the bottom extends past the wheel face by the same
		// amount so the shadow emerges from beneath the wheel.
		// A 45° Y-shear (pivoted on the wheel's right edge) tilts the whole
		// path down-right as if lit from the upper-left.
		const float padding = static_cast<float>(shadowBlur.blurRadius) + 1.0f;
		const float bodyHalfHeight = (std::max)(1.0f, layout.wheelRadius);
		const float bodyCenterY    = layout.wheelCenterY + layout.slotCornerR;
		const float cornerR        = layout.slotCornerR;
		const float arcXRadius     = layout.wheelRadius * 0.5f;
		const float flatWidth      = getWidth(layout.face);

		const float bitmapWidth  = cornerR + flatWidth + arcXRadius + padding;
		const float bitmapHeight = 2.0f * bodyHalfHeight + 2.0f * padding;
		gmpi::drawing::Rect bitmapRect{ 0.0f, 0.0f, bitmapWidth, bitmapHeight };

		constexpr float kPi = 3.14159265f;
		const auto orig = g.getTransform();
		const auto translate = makeTranslation({
			layout.slot.right,
			bodyCenterY - bodyHalfHeight - padding
		});
		const auto skew = makeSkew(0.0f, kPi * 0.25f, { layout.slot.right, 0.0f });
		g.setTransform(translate * skew * orig);

		shadowBlur.draw(g, bitmapRect, [&](Graphics& mask)
			{
				auto brush = mask.createSolidColorBrush(Colors::White);

				const float topY      = padding;
				const float bottomY   = padding + 2.0f * bodyHalfHeight;
				const float leftX     = 0.0f;
				const float arcStartX = /*cornerR +*/ flatWidth;

				auto geometry = mask.getFactory().createPathGeometry();
				auto sink = geometry.open();
				sink.beginFigure({ cornerR, topY }, FigureBegin::Filled);
				sink.addLine({ arcStartX, topY });
				sink.addArc({ { arcStartX, bottomY }, { arcXRadius, bodyHalfHeight }, 0.f, SweepDirection::Clockwise, ArcSize::Small });
				sink.addLine({ cornerR, bottomY });
				sink.addArc({ { leftX, bottomY - cornerR }, { cornerR, cornerR }, 0.f, SweepDirection::Clockwise, ArcSize::Small });
				sink.addLine({ leftX, topY + cornerR });
				sink.addArc({ { cornerR, topY }, { cornerR, cornerR }, 0.f, SweepDirection::Clockwise, ArcSize::Small });
				sink.endFigure();
				sink.close();

				mask.fillGeometry(geometry, brush);
			});
		g.setTransform(orig);

		return ReturnCode::Ok;
	}

	ReturnCode drawWheel(Graphics& g, const Rect& localBounds)
	{
		const auto layout = computeLayout(localBounds);

		// ── Slot recess (the channel the wheel emerges from) ────────────────
		// Almost-black with a faint vertical gradient (slightly darker at top).
		{
			auto slotBrush = g.createLinearGradientBrush(
				{ layout.slot.left, layout.slot.top },
				{ layout.slot.left, layout.slot.bottom },
				Color{ 0.02f, 0.02f, 0.02f, 1.0f },
				Color{ 0.08f, 0.08f, 0.08f, 1.0f }
			);
			g.fillRoundedRectangle(
				{ layout.slot, layout.slotCornerR, layout.slotCornerR },
				slotBrush
			);
		}

		// ── Cylinder face — vertical gradient suggesting curvature ──────────
		// Brightest above centre (lit from above), darker toward top/bottom
		// where the rim curves away from us.
		{
			const auto fillColor = getFillColor();
			const auto highlight = interpolateColor(fillColor, Colors::White, 0.55f);
			const auto midtone   = fillColor;
			const auto edgeDark  = interpolateColor(fillColor, Colors::Black, 0.65f);

			Gradientstop cylinderStops[] = {
				{ 0.00f, edgeDark  },
				{ 0.40f, highlight },
				{ 0.60f, midtone   },
				{ 1.00f, edgeDark  },
			};

			auto faceBrush = g.createLinearGradientBrush(
				cylinderStops,
				{ layout.face.left, layout.face.top },
				{ layout.face.left, layout.face.bottom }
			);
			g.fillRoundedRectangle(
				{ layout.face, layout.faceCornerR, layout.faceCornerR },
				faceBrush
			);
		}

		// ── Position notch — second pass: coloured stripe over a transparent
		// background. Filling the same rounded rectangle clips the stripe at
		// the face's rounded ends naturally. The stripe's top/bottom Y are
		// foreshortened in computeLayout() so the stripe narrows at the extremes.
		{
			const float faceHeight = (std::max)(1.0f, getHeight(layout.face));
			const float stripeTop = (layout.stripeTopY - layout.face.top) / faceHeight;
			const float stripeBot = (layout.stripeBotY - layout.face.top) / faceHeight;
			constexpr float verySmall = 0.005f;

			constexpr Color kTransparent{ 0.0f, 0.0f, 0.0f, 0.0f };
			const auto lineColor = getIndicatorColor();

			// Sharp transitions: transparent → line → transparent. ExtendMode::Clamp
			// keeps everything above/below the stripe transparent.
			Gradientstop stripeStops[] = {
				{ stripeTop - verySmall, kTransparent },
				{ stripeTop,             lineColor    },
				{ stripeBot,             lineColor    },
				{ stripeBot + verySmall, kTransparent },
			};

			auto stripeBrush = g.createLinearGradientBrush(
				stripeStops,
				{ layout.face.left, layout.face.top },
				{ layout.face.left, layout.face.bottom }
			);
			g.fillRoundedRectangle(
				{ layout.face, layout.faceCornerR, layout.faceCornerR },
				stripeBrush
			);
		}

		return ReturnCode::Ok;
	}

public:
	ModWheelGui() = default;

	ReturnCode getClipArea(Rect* returnRect) override
	{
		const auto width = (std::max)(1u, static_cast<uint32_t>(getWidth(bounds)));
		const auto height = (std::max)(1u, static_cast<uint32_t>(getHeight(bounds)));
		const auto localBounds = getLocalBounds({ width, height });
		const auto layout = computeLayout(localBounds);

		const float padding = static_cast<float>(getShadowBlurRadius()) + 1.0f;
		const float bodyHalfHeight = (std::max)(1.0f, layout.wheelRadius);
		const float bodyCenterY    = layout.wheelCenterY + layout.slotCornerR;
		const float cornerR        = layout.slotCornerR;
		const float arcXRadius     = layout.wheelRadius * 0.5f;
		const float flatWidth      = getWidth(layout.face);
		const float shadowRightExtent = cornerR + flatWidth + arcXRadius;
		// 45° Y-shear pivoted on slot.right shifts the bitmap's right edge down
		// by (bitmap right extent - slot.right). Top of the bitmap stays put
		// because its left edge sits on the pivot.
		const float shadowRight  = layout.slot.right + shadowRightExtent + padding;
		const float shadowBottom = bodyCenterY + bodyHalfHeight + shadowRightExtent + 2.0f * padding;

		*returnRect = {
			bounds.left,
			bounds.top,
			(std::max)(bounds.right,  bounds.left + shadowRight),
			(std::max)(bounds.bottom, bounds.top + shadowBottom)
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
			return drawWheel(g, getLocalBounds(size));
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
		if ((flags & static_cast<int32_t>(gmpi::api::PointerFlags::FirstButton)) == 0)
			return ReturnCode::Ok;

		pointPrevious = point;
		pinMouseDown = true;

		if (inputHost.get())
			inputHost->setCapture();

		const auto width = (std::max)(1u, static_cast<uint32_t>(getWidth(bounds)));
		const auto height = (std::max)(1u, static_cast<uint32_t>(getHeight(bounds)));
		const auto localBounds = getLocalBounds({ width, height });

		return ReturnCode::Ok;
	}

	ReturnCode onPointerMove(Point point, int32_t flags) override
	{
		if (!hasCapture())
			return ReturnCode::Unhandled;

		const auto width = (std::max)(1u, static_cast<uint32_t>(getWidth(bounds)));
		const auto height = (std::max)(1u, static_cast<uint32_t>(getHeight(bounds)));
		const auto localBounds = getLocalBounds({ width, height });

		const bool fineControl = (flags & static_cast<int32_t>(gmpi::api::PointerFlags::KeyControl)) != 0;
		const float coarseness = fineControl ? 0.0005f : 0.005f;

		auto newValue = pinValue.value + coarseness * (point.y - pointPrevious.y);
		pinValue = (std::clamp)(newValue, 0.0f, 1.0f);

		pointPrevious = point;
		return ReturnCode::Ok;
	}

	ReturnCode onPointerUp(Point point, int32_t flags) override
	{
		if (!hasCapture())
			return ReturnCode::Unhandled;

		if (inputHost.get())
			inputHost->releaseCapture();

		pinMouseDown = false;
		return ReturnCode::Ok;
	}
};

namespace
{
auto r = gmpi::Register<ModWheelGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE ModWheel" name="ModWheel" category="Sub-Controls">
    <GUI graphicsApi="GmpiGui">
        <Pin name="Normalized" datatype="float" default="0.0"/>
		<Pin name="Menu Items" datatype="string"/>
		<Pin name="Menu Selection" datatype="int"/>
		<Pin name="Mouse Down" datatype="bool"/>
		<Pin name="Hint" datatype="string"/>
		<Pin name="Base Color" datatype="string" default="888888"/>
		<Pin name="Line Color" datatype="string" default="CC111111"/>
    </GUI>
</Plugin>
)XML");
}

// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.

#include "helpers/GmpiPluginEditor.h"
#include <algorithm>
#include <cmath>

using namespace gmpi;
using namespace gmpi::drawing;
using namespace gmpi::editor;

class VUMeterGui final : public PluginEditor
{
	Pin<float> pinValueDb;

	TextFormat labelFormat;
	TextFormat vuFormat;
	float cachedFontHeight = 0.0f;

	static constexpr float kPi = 3.14159265f;

	// Standard VU meter scale: non-linear dB positions mapped to 0-1 of sweep.
	// Spacing follows IEC 60268-17 / ANSI C16.5 proportions.
	struct ScaleMark { float dB; float position; const char* label; bool redZone; };
	static const ScaleMark kScale[];
	static constexpr int kScaleCount = 11;

	// Map a dB value to a 0-1 sweep position by interpolating the scale table
	float dbToPosition(float dB) const
	{
		if (dB <= kScale[0].dB)
			return kScale[0].position;
		if (dB >= kScale[kScaleCount - 1].dB)
			return kScale[kScaleCount - 1].position;

		for (int i = 0; i < kScaleCount - 1; ++i)
		{
			if (dB <= kScale[i + 1].dB)
			{
				const float t = (dB - kScale[i].dB) / (kScale[i + 1].dB - kScale[i].dB);
				return kScale[i].position + t * (kScale[i + 1].position - kScale[i].position);
			}
		}
		return kScale[kScaleCount - 1].position;
	}

	static float posToAngle(float pos, float halfSweep)
	{
		return -halfSweep + pos * 2.0f * halfSweep;
	}

	// Point on an elliptical arc centred at pivot
	static Point arcPoint(Point pivot, float rx, float ry, float angle)
	{
		return { pivot.x + rx * std::sin(angle), pivot.y - ry * std::cos(angle) };
	}

	// Point extended outward along the pivot-to-arc radial direction
	static Point radialPoint(Point pivot, float rx, float ry, float angle, float extend)
	{
		const float sx = rx * std::sin(angle);
		const float cy = ry * std::cos(angle);
		const float len = std::sqrt(sx * sx + cy * cy);
		const float scale = (len + extend) / len;
		return { pivot.x + sx * scale, pivot.y - cy * scale };
	}

	void redraw()
	{
		if (drawingHost)
			drawingHost->invalidateRect(&bounds);
	}

public:
	VUMeterGui()
	{
		pinValueDb.onUpdate = [this](PinBase*) { redraw(); };
	}

	ReturnCode measure(const Size* availableSize, Size* returnDesiredSize) override
	{
		*returnDesiredSize = *availableSize;
		returnDesiredSize->width  = (std::max)(60.0f, returnDesiredSize->width);
		returnDesiredSize->height = (std::max)(40.0f, returnDesiredSize->height);
		return ReturnCode::Ok;
	}

	ReturnCode render(drawing::api::IDeviceContext* dc) override
	{
		Graphics g(dc);
		ClipDrawingToBounds clip(g, bounds);

		const float w = getWidth(bounds);
		const float h = getHeight(bounds);
		const float cx = bounds.left + w * 0.5f;

		// Elliptical arc geometry:
		//   pivot:      y = bottom + h/3
		//   arc peak:   y = top + h/3      → Ry = h
		//   arc ends:   y = top + h/2, x = w/6 from edges → Rx = 2w/sqrt(11)
		//   halfSweep = acos(5/6)  (parametric angle, from y constraint) 
		const float ry        = h;
		const float rx        = 2.0f * w / std::sqrt(11.0f);
		const float halfSweep = std::acos(5.0f / 6.0f);
		const Point pivot{ cx, bounds.bottom + h / 3.0f };

		const float tickMaj  = h / 6.0f;
		const float tickMin  = tickMaj * 0.5f;
		const float strokeW  = (std::max)(1.0f,  w * 0.010f);
		const float strokeWm = (std::max)(0.5f,  w * 0.006f);

		// ── Background with radial gradient glow ────────────────────────────
		{
			// Radius reaches the far corners from centre
			const float dx = w * 0.5f;
			const float dy = h * 0.5f;
			const float cornerR = std::sqrt(dx * dx + dy * dy);

			auto bgBrush = g.createRadialGradientBrush(
				{ cx, bounds.top + h * 0.5f },
				cornerR,
				colorFromHex(0xFEEC7Cu),   // centre
				colorFromHex(0xF38524u)    // edge
			);
			g.fillRectangle(bounds, bgBrush);
		}

		// ── Brushes ─────────────────────────────────────────────────────────
		auto darkBrush = g.createSolidColorBrush(Color{ 0.12f, 0.12f, 0.12f, 1.0f });
		auto redBrush  = g.createSolidColorBrush(Color{ 0.78f, 0.12f, 0.10f, 1.0f });

		// ── Text formats (recreate when height changes) ────────────────────
		if (cachedFontHeight != h)
		{
			cachedFontHeight = h;
			std::vector<std::string_view> fam = { "Arial" };
			const float fontSize = (std::max)(7.0f, h / 7.0f);
			labelFormat = g.getFactory().createTextFormat(
				fontSize, fam, FontWeight::Bold, FontStyle::Normal);
			labelFormat.setTextAlignment(TextAlignment::Center);
			labelFormat.setParagraphAlignment(ParagraphAlignment::Center);
			vuFormat = g.getFactory().createTextFormat(
				fontSize * 1.2f, fam, FontWeight::Bold, FontStyle::Normal);
			vuFormat.setTextAlignment(TextAlignment::Center);
			vuFormat.setParagraphAlignment(ParagraphAlignment::Center);
		}

		// Half a tick-width as an angular extension so the arc
		// reaches to the outer edges of the first and last ticks.
		const float tickHalfAngle = (strokeW * 0.5f) / (std::max)(rx, ry);

		// ── Scale arc (full sweep) ──────────────────────────────────────────
		{
			auto geom = g.getFactory().createPathGeometry();
			auto sink = geom.open();
			sink.beginFigure(arcPoint(pivot, rx, ry, posToAngle(0, halfSweep) - tickHalfAngle), FigureBegin::Hollow);
			sink.addArc({ arcPoint(pivot, rx, ry, posToAngle(1, halfSweep) + tickHalfAngle),
				{ rx, ry }, 0, SweepDirection::Clockwise, ArcSize::Small });
			sink.endFigure(FigureEnd::Open);
			sink.close();
			g.drawGeometry(geom, darkBrush, strokeW);
		}

		// ── Red-zone arc overlay (0 dB → +3 dB) ────────────────────────────
		{
			const float redStrokeW = strokeW * 1.4f;
			const float redOffset  = (redStrokeW - strokeW) * 0.5f;
			auto geom = g.getFactory().createPathGeometry();
			auto sink = geom.open();
			sink.beginFigure(radialPoint(pivot, rx, ry, posToAngle(kScale[7].position, halfSweep) + tickHalfAngle, -redOffset), FigureBegin::Hollow);
			sink.addArc({ radialPoint(pivot, rx, ry, posToAngle(1, halfSweep) + tickHalfAngle, -redOffset),
				{ rx - redOffset, ry - redOffset }, 0, SweepDirection::Clockwise, ArcSize::Small });
			sink.endFigure(FigureEnd::Open);
			sink.close();
			g.drawGeometry(geom, redBrush, redStrokeW);
		}

		// ── Ticks & labels ──────────────────────────────────────────────────
		for (int i = 0; i < kScaleCount; ++i)
		{
			const auto& m = kScale[i];
			const float a = posToAngle(m.position, halfSweep);
			auto& brush = m.redZone ? redBrush : darkBrush;

			// major tick (extends outward from arc along radial direction)
			g.drawLine(arcPoint(pivot, rx, ry, a),
			           radialPoint(pivot, rx, ry, a, tickMaj), brush, strokeW);

			// label
			const float labelNudge = (i == 0) ? 1.0f : (i == 1) ? 0.5f : 0.0f;
			const auto tp = radialPoint(pivot, rx, ry, a, tickMaj + h / 7.0f * 0.6f + labelNudge);
			const float tw = h * 0.12f;
			const Rect tr{ tp.x - tw, tp.y - tw * 0.7f, tp.x + tw, tp.y + tw * 0.7f };
			g.drawTextU(m.label, labelFormat, tr, brush);

			// minor ticks between this mark and the next
			if (i + 1 < kScaleCount)
			{
				const float span = kScale[i + 1].position - m.position;
				const int n = span > 0.12f ? 4 : 1;
				for (int j = 1; j <= n; ++j)
				{
					const float mp = m.position + span * static_cast<float>(j) / static_cast<float>(n + 1);
					const float ma = posToAngle(mp, halfSweep);
					g.drawLine(arcPoint(pivot, rx, ry, ma),
					           radialPoint(pivot, rx, ry, ma, tickMin),
					           (m.redZone || kScale[i + 1].redZone) ? redBrush : darkBrush, strokeWm);
				}
			}
		}

		// ── "VU" label ──────────────────────────────────────────────────────
		{
			const float vuTop = bounds.top + h * 0.5f;  // same y as arc endpoints
			const float vuFontH = (std::max)(7.0f, h / 7.0f) * 1.2f;
			const Rect vuRect{
				cx - w * 0.12f,
				vuTop,
				cx + w * 0.12f,
				vuTop + vuFontH * 1.4f
			};
			g.drawTextU("VU", vuFormat, vuRect, darkBrush);
		}

		// ── Needle ──────────────────────────────────────────────────────────
		{
			const float norm  = dbToPosition(std::clamp(pinValueDb.value, -20.0f, 3.0f));
			const float angle = posToAngle(norm, halfSweep);

			const auto tip  = radialPoint(pivot, rx, ry, angle, -ry * 0.003f);
			const auto base = arcPoint(pivot, rx * 0.06f, ry * 0.06f, angle);

			auto needleBrush = g.createSolidColorBrush(Color{ 0.08f, 0.08f, 0.08f, 1.0f });
			g.drawLine(base, tip, needleBrush, (std::max)(1.2f, w * 0.008f));
		}

		return ReturnCode::Ok;
	}
};

// Non-linear VU scale: { dB, sweep position 0-1, label, redZone }
const VUMeterGui::ScaleMark VUMeterGui::kScale[] = {
	{ -20.0f, 0.000f, "20", false },
	{ -10.0f, 0.167f, "10", false },
	{  -7.0f, 0.283f,  "7", false },
	{  -5.0f, 0.392f,  "5", false },
	{  -3.0f, 0.517f,  "3", false },
	{  -2.0f, 0.600f,  "2", false },
	{  -1.0f, 0.683f,  "1", false },
	{   0.0f, 0.770f,  "0", false },
	{   1.0f, 0.850f,  "1", true  },
	{   2.0f, 0.925f,  "2", true  },
	{   3.0f, 1.000f,  "3", true  },
};

namespace
{
auto r = gmpi::Register<VUMeterGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE VU Meter" name="VU Meter" category="Sub-Controls">
    <GUI graphicsApi="GmpiGui">
        <Pin name="Value dB" datatype="float" default="-20.0"/>
    </GUI>
</Plugin>
)XML");
}

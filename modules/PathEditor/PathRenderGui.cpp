// SPDX-License-Identifier: ISC
// Copyright 2026 Jeff McClintock.

#include "../Controls/ControlsBase.h"
#include "helpers/PixelSnapper.h"
#include <cctype>
#include <cstdlib>

// ── Path renderer ───────────────────────────────────────────────────────────
// Sister to "SE Path Editor". Takes a path "d"-string and draws it with the
// configured fill/stroke. No editing — just rendering.
//
// Pins
//   Path          — SVG-subset d-string (M L H V C Z, both absolute and relative)
//   Fill Color    — "RRGGBB" or "AARRGGBB". Empty string = don't fill
//   Stroke Color  — same. Empty string = don't stroke
//   Stroke Width  — pixels
//   Snap to Pixels — when true, every point in the geometry is snapped to the
//                    nearest hardware pixel (crisper rendering for axis-aligned
//                    shapes; geometry rebuilt each frame since it depends on
//                    the current transform and DPI)

class PathRenderGui final : public ControlsBase
{
	Pin<std::string> pinPath;
	Pin<std::string> pinFillColor;
	Pin<std::string> pinStrokeColor;
	Pin<float>       pinStrokeWidth;
	Pin<bool>        pinSnapPixels;

	// Geometry cache. Pin changes (Path, Snap to Pixels) clear cachedGeometry
	// via onUpdate. The transform / DPI / parity invalidation is handled by
	// keeping a stateful pixelSnapper2 as a member: its update() returns true
	// iff anything changed since the previous frame.
	PathGeometry  cachedGeometry;
	bool          cachedCentreSnap = false;
	pixelSnapper2 snapper;

	// Build a PathGeometry directly from the d-string. Supports M/L/H/V/C/Z and
	// their lowercase (relative) variants. `snap` is applied to every point
	// before it hits the sink — pass an identity lambda to disable snapping.
	template<typename SnapFn>
	static PathGeometry parsePathToGeometry(Graphics& g, const std::string& d, SnapFn snap)
	{
		auto geom = g.getFactory().createPathGeometry();
		auto sink = geom.open();

		struct Tok { char cmd; std::vector<float> args; };
		std::vector<Tok> tokens;
		for(auto p = d.c_str(); *p; ++p)
		{
			if(std::isalpha(static_cast<unsigned char>(*p)))
			{
				tokens.push_back({ *p });
			}
			else if(std::isdigit(static_cast<unsigned char>(*p)) || *p == '-' || *p == '.')
			{
				if(tokens.empty()) continue;
				char* endp = nullptr;
				tokens.back().args.push_back(static_cast<float>(std::strtod(p, &endp)));
				if(endp <= p) break;
				p = endp - 1;
			}
		}

		Point   last{};
		Point   first{};
		bool    inFigure   = false;
		bool    closeOnEnd = false;

		auto endFig = [&]
		{
			if(inFigure)
			{
				sink.endFigure(closeOnEnd ? FigureEnd::Closed : FigureEnd::Open);
				inFigure = false;
				closeOnEnd = false;
			}
		};

		// SVG spec: when a draw command (L/H/V/C) is encountered while no
		// figure is open (e.g. after a stray "Z L ..." in the d-string), the
		// implicit current point is the most recent M. Start a figure there.
		auto ensureFigure = [&]
		{
			if(!inFigure)
			{
				sink.beginFigure(snap(last), FigureBegin::Filled);
				inFigure = true;
				first = last;
			}
		};

		for(const auto& t : tokens)
		{
			switch(t.cmd)
			{
			case 'M': case 'm':
			{
				const bool rel = (t.cmd == 'm');
				if(t.args.size() < 2) break;
				endFig();
				Point p{ t.args[0], t.args[1] };
				if(rel && (last.x != 0.0f || last.y != 0.0f)) { p.x += last.x; p.y += last.y; }
				sink.beginFigure(snap(p), FigureBegin::Filled);
				inFigure = true;
				last = first = p;
				// Subsequent coord pairs after M are implicit Line-to.
				for(size_t i = 1; i + 1 < t.args.size(); i += 2)
				{
					Point q{ t.args[i], t.args[i + 1] };
					if(rel) { q.x += last.x; q.y += last.y; }
					sink.addLine(snap(q));
					last = q;
				}
			}
			break;

			case 'L': case 'l':
			{
				const bool rel = (t.cmd == 'l');
				for(size_t i = 0; i + 1 < t.args.size(); i += 2)
				{
					Point q{ t.args[i], t.args[i + 1] };
					if(rel) { q.x += last.x; q.y += last.y; }
					ensureFigure();
					sink.addLine(snap(q));
					last = q;
				}
			}
			break;

			case 'H': case 'h':
			{
				const bool rel = (t.cmd == 'h');
				for(float x : t.args)
				{
					last.x = rel ? last.x + x : x;
					ensureFigure();
					sink.addLine(snap(last));
				}
			}
			break;

			case 'V': case 'v':
			{
				const bool rel = (t.cmd == 'v');
				for(float y : t.args)
				{
					last.y = rel ? last.y + y : y;
					ensureFigure();
					sink.addLine(snap(last));
				}
			}
			break;

			case 'C': case 'c':
			{
				const bool rel = (t.cmd == 'c');
				for(size_t i = 0; i + 5 < t.args.size(); i += 6)
				{
					Point c1{ t.args[i],     t.args[i + 1] };
					Point c2{ t.args[i + 2], t.args[i + 3] };
					Point pe{ t.args[i + 4], t.args[i + 5] };
					if(rel)
					{
						c1.x += last.x; c1.y += last.y;
						c2.x += last.x; c2.y += last.y;
						pe.x += last.x; pe.y += last.y;
					}
					ensureFigure();
					sink.addBezier({ snap(c1), snap(c2), snap(pe) });
					last = pe;
				}
			}
			break;

			case 'Z': case 'z':
				closeOnEnd = true;
				last = first;
				endFig();
				break;

			default:
				break;
			}
		}

		endFig();
		sink.close();
		return geom;
	}

public:
	PathRenderGui()
	{
		// Pins that change the geometry: invalidate the cache directly here so
		// render() doesn't have to compare strings or bools every frame.
		auto invalidate = [this](PinBase*) { cachedGeometry = {}; redraw(); };
		pinPath.onUpdate       = invalidate;
		pinSnapPixels.onUpdate = invalidate;

		// Pins that only affect the brushes — geometry is untouched.
		auto redrawOnly = [this](PinBase*) { redraw(); };
		pinFillColor.onUpdate   = redrawOnly;
		pinStrokeColor.onUpdate = redrawOnly;
		pinStrokeWidth.onUpdate = redrawOnly; // a parity flip is detected by the centreSnap check in render()
	}

	ReturnCode render(drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);
		ClipDrawingToBounds _(g, bounds);

		if(pinPath.value.empty())
			return ReturnCode::Ok;

		const bool snap = pinSnapPixels.value;
		const float rs = drawingHost.get() ? drawingHost->getRasterizationScale() : 1.0f;

		// Refresh the snapper from the live transform & DPI. Returns true iff
		// anything changed since the previous frame; we use that to know when
		// any snap-dependent cache (here: the geometry) needs rebuilding.
		const bool transformChanged = snapper.update(g.getTransform(), rs);

		const bool wantStroke = !pinStrokeColor.value.empty() && pinStrokeWidth.value > 0.0f;

		// Snap the stroke width (and learn whether it landed on an odd pixel
		// count, which means the geometry needs pixel-centre alignment for a
		// crisp render). snapForStroke() picks centre vs origin from the
		// lineSnap, so the caller doesn't have to.
		float snappedWidth = (std::max)(0.0f, pinStrokeWidth.value);
		pixelSnapper2::lineSnap ls{ snappedWidth, 0.0f };
		if(snap && wantStroke)
		{
			ls = snapper.thickness(snappedWidth);
			snappedWidth = ls.width;
		}
		const bool centreSnap = ls.center_offset != 0.0f;

		auto snapFn = [&](Point p) { return snapper.snapForStroke(p, ls); };

		// Cache check. pinPath / pinSnapPixels invalidate via onUpdate; what's
		// left to detect here is "the transform changed" (zoom, pan, DPI) and
		// "the snapped stroke parity flipped" — both possible without any pin
		// notification.
		const bool needRebuild =
			!cachedGeometry
			|| (snap && (cachedCentreSnap != centreSnap || transformChanged));

		if(needRebuild)
		{
			if(snap)
			{
				cachedGeometry = parsePathToGeometry(g, pinPath.value, snapFn);
			}
			else
			{
				auto identity = [](Point p) { return p; };
				cachedGeometry = parsePathToGeometry(g, pinPath.value, identity);
			}
			cachedCentreSnap = centreSnap;
		}

		if(!pinFillColor.value.empty())
		{
			auto fillBrush = g.createSolidColorBrush(colorFromHexString(pinFillColor.value));
			g.fillGeometry(cachedGeometry, fillBrush);
		}

		if(wantStroke)
		{
			auto strokeBrush = g.createSolidColorBrush(colorFromHexString(pinStrokeColor.value));
			g.drawGeometry(cachedGeometry, strokeBrush, snappedWidth);
		}

		return ReturnCode::Ok;
	}
};

namespace
{
auto r = gmpi::Register<PathRenderGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Path Render" name="Path Render" category="Experimental">
    <GUI graphicsApi="GmpiGui">
        <Pin name="Path" datatype="string"/>
        <Pin name="Fill Color" datatype="string" default="EEEEEE"/>
        <Pin name="Stroke Color" datatype="string" default="000000"/>
        <Pin name="Stroke Width" datatype="float" default="1.0"/>
        <Pin name="Snap to Pixels" datatype="bool" private="true"/>
    </GUI>
</Plugin>
)XML");
}
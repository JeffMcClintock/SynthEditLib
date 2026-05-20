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

	// Geometry cache. We rebuild only when one of the inputs that actually
	// affects the geometry has changed since the last frame — for the snap-off
	// case that's just the path string, for snap-on it's also the transform
	// (zoom + DPI + module position) and the stroke-parity flag.
	PathGeometry cachedGeometry;
	struct CacheKey
	{
		std::string path;
		bool snap       = false;
		bool centreSnap = false;
		Matrix3x2 transform{};   // only consulted when snap == true
	} cachedKey;

	static bool matrixEq(const Matrix3x2& a, const Matrix3x2& b)
	{
		return a._11 == b._11 && a._12 == b._12
		    && a._21 == b._21 && a._22 == b._22
		    && a._31 == b._31 && a._32 == b._32;
	}

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
		// Every pin just kicks a redraw; the geometry-cache key in render()
		// notices what actually changed and rebuilds only if it has to.
		auto redrawCallback = [this](PinBase*) { redraw(); };
		pinPath.onUpdate        = redrawCallback;
		pinFillColor.onUpdate   = redrawCallback;
		pinStrokeColor.onUpdate = redrawCallback;
		pinStrokeWidth.onUpdate = redrawCallback;
		pinSnapPixels.onUpdate  = redrawCallback;
	}

	ReturnCode render(drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);
		ClipDrawingToBounds _(g, bounds);

		if(pinPath.value.empty())
			return ReturnCode::Ok;

		const bool snap = pinSnapPixels.value;
		const float rs = drawingHost.get() ? drawingHost->getRasterizationScale() : 1.0f;
		// Cheap to construct (one matrix invert); shared by geometry snapping
		// below and stroke-width snapping further down.
		pixelSnapper2 snapper(g.getTransform(), rs);

		const bool wantStroke = !pinStrokeColor.value.empty() && pinStrokeWidth.value > 0.0f;

		// Compute the snapped stroke width and whether the snapped width is an
		// odd number of physical pixels. An odd width must straddle a pixel
		// centre to render crisply, so we have to snap the path's points to
		// pixel *centres* (integer + 0.5) instead of pixel origins. Even widths
		// (and the fill-only case) snap to origins.
		float snappedWidth = (std::max)(0.0f, pinStrokeWidth.value);
		bool centreSnap = false;
		if(snap && wantStroke)
		{
			const auto ls = snapper.thickness(snappedWidth);
			snappedWidth = ls.width;
			centreSnap = ls.center_offset != 0.0f;
		}

		auto snapFn = [&](Point p) -> Point
		{
			if(centreSnap)
			{
				Point pp = transformPoint(snapper.transform, p);
				pp.x = std::floor(pp.x) + 0.5f;
				pp.y = std::floor(pp.y) + 0.5f;
				return transformPoint(snapper.inverted, pp);
			}
			return snapper.snapPixelOrigin(p);
		};

		// Cache check. Snap-off depends only on the path string; snap-on also
		// depends on the transform (zoom + DPI + module position) and the
		// odd/even parity of the snapped stroke. Editing happens at typing
		// speed but rendering happens at frame rate, so on a steady state this
		// hits every frame and parsePathToGeometry never runs.
		const bool needRebuild =
			!cachedGeometry
			|| cachedKey.snap != snap
			|| cachedKey.path != pinPath.value
			|| (snap && (cachedKey.centreSnap != centreSnap
			             || !matrixEq(cachedKey.transform, snapper.transform)));

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
			cachedKey.path       = pinPath.value;
			cachedKey.snap       = snap;
			cachedKey.centreSnap = centreSnap;
			cachedKey.transform  = snapper.transform;
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
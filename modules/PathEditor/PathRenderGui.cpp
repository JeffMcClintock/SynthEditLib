// SPDX-License-Identifier: ISC
// Copyright 2026 Jeff McClintock.

#include "../Controls/ControlsBase.h"
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

class PathRenderGui final : public ControlsBase
{
	Pin<std::string> pinPath;
	Pin<std::string> pinFillColor;
	Pin<std::string> pinStrokeColor;
	Pin<float>       pinStrokeWidth;

	PathGeometry cachedGeometry;
	std::string  cachedGeometrySource;  // the d-string that built `cachedGeometry`

	// Build a PathGeometry directly from the d-string. Supports M/L/H/V/C/Z and
	// their lowercase (relative) variants.
	static PathGeometry parsePathToGeometry(Graphics& g, const std::string& d)
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
				sink.beginFigure(last, FigureBegin::Filled);
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
				sink.beginFigure(p, FigureBegin::Filled);
				inFigure = true;
				last = first = p;
				// Subsequent coord pairs after M are implicit Line-to.
				for(size_t i = 1; i + 1 < t.args.size(); i += 2)
				{
					Point q{ t.args[i], t.args[i + 1] };
					if(rel) { q.x += last.x; q.y += last.y; }
					sink.addLine(q);
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
					sink.addLine(q);
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
					sink.addLine(last);
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
					sink.addLine(last);
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
					sink.addBezier({ c1, c2, pe });
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
		pinPath.onUpdate        = [this](PinBase*) { cachedGeometry = {}; redraw(); };
		pinFillColor.onUpdate   = [this](PinBase*) { redraw(); };
		pinStrokeColor.onUpdate = [this](PinBase*) { redraw(); };
		pinStrokeWidth.onUpdate = [this](PinBase*) { redraw(); };
	}

	ReturnCode render(drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);
		ClipDrawingToBounds _(g, bounds);

		if(pinPath.value.empty())
			return ReturnCode::Ok;

		// (Re)build cached geometry on first use / when the path string changes.
		if(!cachedGeometry || cachedGeometrySource != pinPath.value)
		{
			cachedGeometry = parsePathToGeometry(g, pinPath.value);
			cachedGeometrySource = pinPath.value;
		}

		if(!pinFillColor.value.empty())
		{
			auto fillBrush = g.createSolidColorBrush(colorFromHexString(pinFillColor.value));
			g.fillGeometry(cachedGeometry, fillBrush);
		}

		if(!pinStrokeColor.value.empty())
		{
			const float width = (std::max)(0.0f, pinStrokeWidth.value);
			if(width > 0.0f)
			{
				auto strokeBrush = g.createSolidColorBrush(colorFromHexString(pinStrokeColor.value));
				g.drawGeometry(cachedGeometry, strokeBrush, width);
			}
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
    </GUI>
</Plugin>
)XML");
}

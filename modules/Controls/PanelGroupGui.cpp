// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.

#include "helpers/GmpiPluginEditor.h"
#include "Extensions/EmbeddedFile.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

using namespace gmpi;
using namespace gmpi::drawing;
using namespace gmpi::editor;

// ── Minimal parser for a FONT_CATEGORY block in a SynthEdit skin global.txt ──
namespace
{
struct SkinFontInfo
{
	std::vector<std::string> families = { "Arial" };
	float      size   = 12.0f;
	FontWeight weight = FontWeight::Regular;
	FontStyle  style  = FontStyle::Normal;
	Color      color  = Colors::White;
};

std::string trimToken(std::string s)
{
	const auto front = [](char c){ return c == ' ' || c == '\t' || c == '"'; };
	while (!s.empty() && front(s.front())) s.erase(s.begin());
	while (!s.empty() && front(s.back()))  s.pop_back();
	return s;
}

SkinFontInfo parseSkinFontCategory(const std::string& filePath, std::string_view category)
{
	SkinFontInfo result;
	std::ifstream file(filePath);
	if (!file.is_open())
		return result;

	bool inSection = false;
	std::string line;
	while (std::getline(file, line))
	{
		// strip leading whitespace; skip blank lines and comments
		const auto start = line.find_first_not_of(" \t");
		if (start == std::string::npos || line[start] == ';')
			continue;
		line = line.substr(start);

		if (line.rfind("FONT_CATEGORY", 0) == 0)
		{
			if (inSection) break;                      // past our section
			inSection = (trimToken(line.substr(13)) == category);
			continue;
		}
		if (!inSection) continue;

		if (line.rfind("font-family", 0) == 0)
		{
			result.families.clear();
			std::stringstream ss(line.substr(11));
			std::string token;
			while (std::getline(ss, token, ','))
				if (auto f = trimToken(token); !f.empty())
					result.families.push_back(f);
		}
		else if (line.rfind("font-size", 0) == 0)
		{
			try { result.size = std::stof(trimToken(line.substr(9))); } catch (...) {}
		}
		else if (line.rfind("font-weight", 0) == 0)
		{
			const auto v = trimToken(line.substr(11));
			if      (v == "bold")  result.weight = FontWeight::Bold;
			else if (v == "light") result.weight = FontWeight::Light;
		}
		else if (line.rfind("font-style", 0) == 0)
		{
			if (trimToken(line.substr(10)) == "italic")
				result.style = FontStyle::Italic;
		}
		else if (line.rfind("font-color", 0) == 0)
		{
			auto v = trimToken(line.substr(10));
			if (!v.empty() && v[0] == '#') v = v.substr(1);
			if (v.size() == 6) v = "FF" + v;            // add full-alpha prefix
			if (v.size() == 8)
				try { result.color = colorFromHexString(v); } catch (...) {}
		}
	}
	return result;
}
} // namespace


class PanelGroupGui final : public PluginEditor
{
	Pin<std::string> pinText;
	Pin<float>       pinStrokeWidth;
	Pin<float>       pinCornerRadius;
	Pin<std::string> pinColor;

	SkinFontInfo skinFont;
	TextFormat   cachedTextFormat;

	void redraw()
	{
		if (drawingHost)
			drawingHost->invalidateRect(&bounds);
	}

	Color getColor() const
	{
		return pinColor.value.empty() ? Colors::White : colorFromHexString(pinColor.value);
	}

	// Lazily creates the TextFormat on first render (factory is only available then).
	TextFormat& getTextFormat(Graphics& g)
	{
		if (!cachedTextFormat)
		{
			std::vector<std::string_view> views(skinFont.families.begin(), skinFont.families.end());
			cachedTextFormat = g.getFactory().createTextFormat(
				skinFont.size,
				views,
				skinFont.weight,
				skinFont.style
			);
		}
		return cachedTextFormat;
	}

public:
	PanelGroupGui()
	{
		pinText.onUpdate         = [this](PinBase*) { redraw(); };
		pinStrokeWidth.onUpdate  = [this](PinBase*) { redraw(); };
		pinCornerRadius.onUpdate = [this](PinBase*) { redraw(); };
		pinColor.onUpdate        = [this](PinBase*) { redraw(); };
	}

	ReturnCode open(gmpi::api::IUnknown* host) override
	{
		const auto r = PluginEditor::open(host);   // sets up drawingHost

		// Locate and parse the skin's global.txt to get the "panel_group" font.
		if (auto synthEdit = drawingHost.as<synthedit::IEmbeddedFileSupport>())
		{
			ReturnString globalUri;
			if(synthEdit->findResourceUri("global.txt", &globalUri) == ReturnCode::Ok)
			{
				// !!returning only "skins/default2/global.txt" not fullURi

				skinFont = parseSkinFontCategory(globalUri.cppString, "panel_group");
			}
		}

		return r;
	}

	ReturnCode render(drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);
// TODO extend bounds		ClipDrawingToBounds clip(g, bounds);

		const auto& r  = bounds;
		const float cr = (std::max)(0.001f, pinCornerRadius.value);
		const float sw = (std::max)(0.5f,   pinStrokeWidth.value);
		auto brush = g.createSolidColorBrush(getColor());

		if (pinText.value.empty())
		{
			g.drawRoundedRectangle(RoundedRect{ r, cr, cr }, brush, sw);
		}
		else
		{
			auto& textFormat = getTextFormat(g);
			const auto textExtent = textFormat.getTextExtentU(pinText.value);

			const float hPad      = 2.0f;
			const float extension = getWidth(r) / 8.0f;
			const float gapStart  = cr + extension;
			const float gapEnd    = (std::max)(gapStart,
				(std::min)(gapStart + textExtent.width + hPad * 2.0f, r.right - cr));

			// Open rounded-rectangle path with a gap at the top-left for the label.
			// Traced clockwise starting from the right end of the gap.
			auto geom = g.getFactory().createPathGeometry();
			auto sink = geom.open();
			sink.beginFigure({ r.left + gapEnd,    r.top          }, FigureBegin::Hollow);
			sink.addLine    ({ r.right - cr,        r.top          });
			sink.addArc     ({ { r.right,        r.top + cr    }, { cr, cr }, 0.0f, SweepDirection::Clockwise, ArcSize::Small });
			sink.addLine    ({ r.right,             r.bottom - cr  });
			sink.addArc     ({ { r.right - cr,   r.bottom      }, { cr, cr }, 0.0f, SweepDirection::Clockwise, ArcSize::Small });
			sink.addLine    ({ r.left + cr,         r.bottom       });
			sink.addArc     ({ { r.left,         r.bottom - cr }, { cr, cr }, 0.0f, SweepDirection::Clockwise, ArcSize::Small });
			sink.addLine    ({ r.left,              r.top + cr     });
			sink.addArc     ({ { r.left + cr,    r.top         }, { cr, cr }, 0.0f, SweepDirection::Clockwise, ArcSize::Small });
			sink.addLine    ({ r.left + gapStart,   r.top          }); // horizontal stub past the corner
			sink.endFigure(FigureEnd::Open);
			sink.close();
			g.drawGeometry(geom, brush, sw);

			// Label: skin font, same colour as the stroke.
			auto textBrush = g.createSolidColorBrush(getColor());
			const Rect textRect{
				r.left + gapStart + hPad,
				r.top - textExtent.height * 0.5f,
				r.left + gapEnd   - hPad,
				r.top + textExtent.height * 0.5f
			};
			g.drawTextU(pinText.value, textFormat, textRect, textBrush);
		}

		return ReturnCode::Ok;
	}
};

namespace
{
auto r = gmpi::Register<PanelGroupGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Panel Group2" name="Panel Group" category="Controls">
    <GUI graphicsApi="GmpiGui">
        <Pin name="Text" datatype="string" default="Group"/>
        <Pin name="Stroke Width" datatype="float" default="2.0"/>
        <Pin name="Corner Radius" datatype="float" default="5.0"/>
        <Pin name="Color" datatype="string" default="FFFFFFFF"/>
    </GUI>
</Plugin>
)XML");
}

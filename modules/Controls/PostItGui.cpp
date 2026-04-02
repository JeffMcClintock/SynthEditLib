// SPDX-License-Identifier: ISC
// Copyright 2026 Jeff McClintock.

#include "helpers/GmpiPluginEditor.h"
#include "helpers/CachedBlur.h"
#include <algorithm>
#include <cmath>

using namespace gmpi;
using namespace gmpi::drawing;
using namespace gmpi::editor;

class PostItGui final : public PluginEditor, public gmpi::api::IDrawingLayer
{
	Pin<std::string> pinText;

	RichTextFormat cachedRichTextFormat;
	std::string cachedText;
	cachedBlur shadowBlur;
	SizeU shadowSize{};

	void redraw()
	{
		if (drawingHost)
		{
			Rect clipArea;
			getClipArea(&clipArea);
			drawingHost->invalidateRect(&clipArea);
		}
	}

	void updateShadowCache(const SizeU& size)
	{
		if (shadowSize.width != size.width || shadowSize.height != size.height)
		{
			shadowBlur.invalidate();
			shadowSize = size;
		}
	}

	RichTextFormat& getRichTextFormat(Graphics& g)
	{
		if (!cachedRichTextFormat || cachedText != pinText.value)
		{
			cachedText = pinText.value;
			cachedRichTextFormat = g.getFactory().createRichTextFormat(
				cachedText,
				12.0f,
				std::vector<std::string_view>{"Segoe UI", "Arial"}
			);
		}
		return cachedRichTextFormat;
	}

	// The note shape: rectangle with bottom-right corner replaced by a curve
	PathGeometry createNoteShape(Graphics& g, const Rect& r, float curlSize)
	{
		auto geom = g.getFactory().createPathGeometry();
		auto sink = geom.open();
		sink.beginFigure({r.left, r.top}, FigureBegin::Filled);
		sink.addLine({r.right, r.top});
		sink.addLine({r.right, r.bottom - curlSize});
		sink.addQuadraticBezier({{r.right - curlSize * 0.4f, r.bottom - curlSize * 0.5f},
		                         {r.right - curlSize, r.bottom}});
		sink.addLine({r.left, r.bottom});
		sink.endFigure(FigureEnd::Closed);
		sink.close();
		return geom;
	}

	float getCurlSize() const
	{
		return (std::min)(getWidth(bounds), getHeight(bounds)) * 0.04f;
	}

	float getShadowExtent() const
	{
		const float minDim = (std::min)(getWidth(bounds), getHeight(bounds));
		const float blurRadius = (std::max)(2.0f, minDim * 0.06f);
		const float offset = minDim * 0.04f;
		return blurRadius + offset + 2.0f;
	}

public:
	PostItGui()
	{
		pinText.onUpdate = [this](PinBase*) { redraw(); };
	}

	ReturnCode getClipArea(Rect* returnRect) override
	{
		*returnRect = bounds;
		const auto ext = getShadowExtent();
		returnRect->left -= ext;
		returnRect->top -= ext;
		returnRect->right += ext;
		returnRect->bottom += ext;
		return ReturnCode::Ok;
	}

	int32_t addRef() override { return PluginEditor::addRef(); }
	int32_t release() override { return PluginEditor::release(); }

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

	ReturnCode renderLayer(drawing::api::IDeviceContext* drawingContext, int32_t layer) override
	{
		Graphics g(drawingContext);
		const auto& r = bounds;
		const float w = getWidth(r);
		const float h = getHeight(r);
		const float curlSize = getCurlSize();
/*
		if (layer == -1)
		{
			// ── Soft drop shadow on background layer ──
			const float minDim = (std::min)(w, h);
			const float shadowOffset = minDim * 0.04f;

			const auto width = (std::max)(1u, static_cast<uint32_t>(w));
			const auto height = (std::max)(1u, static_cast<uint32_t>(h));
			const SizeU size{width, height};
			updateShadowCache(size);

			shadowBlur.tint = Color{0.0f, 0.0f, 0.0f, 0.45f};
			shadowBlur.blurRadius = (std::max)(2, static_cast<int>(std::ceil(minDim * 0.06f)));

			const auto orig = g.getTransform();
			g.setTransform(makeTranslation({shadowOffset, shadowOffset}) * orig);
			shadowBlur.draw(g, Rect{0.0f, 0.0f, w, h}, [&](Graphics& mask)
			{
				auto brush = mask.createSolidColorBrush(Colors::White);
				auto noteShape = createNoteShape(mask, Rect{0.0f, 0.0f, w, h}, curlSize);
				mask.fillGeometry(noteShape, brush);
			});
			g.setTransform(orig);

			return ReturnCode::Ok;
		}
*/

		if (layer == 0)
		{
			// ── Yellow palette ──
			const Color stickyTop = colorFromHex(0xFAE37D, 0.9f); // dirty yellow over glue
			const Color yellowTop = colorFromHex(0xFFE479, 0.9f);
			const Color yellowBottom = colorFromHex(0xFFED8B, 0.9f);

//			const Color yellowTop   {1.0f, 0.97f, 0.70f, 0.95f};  // light warm yellow
			//const Color yellowBottom{1.0f, 0.92f, 0.55f, 0.95f};  // slightly richer
			//const Color yellowCurl  {0.88f, 0.80f, 0.35f, 0.95f};  // darker fold underside
			//const Color yellowCurlLt{0.98f, 0.94f, 0.62f, 0.95f};  // lighter fold edge

			// ── Note body ──
			auto noteGeom = createNoteShape(g, r, curlSize);

			Gradientstop gradientStops[] =
			{
				{0.00f, stickyTop}, //dirty yellow over glue
				{0.15f, stickyTop}, //dirty yellow over glue
				{0.17f, yellowTop}, //dirty yellow over glue
				{1.0f, yellowBottom }
			};

			auto fillBrush = g.createLinearGradientBrush(
				gradientStops,
				{r.left, r.top}, {r.left, r.bottom}
			);

			{
				auto curlGeom = g.getFactory().createPathGeometry();
				auto sink = curlGeom.open();

				sink.beginFigure({ r.left, r.top }, FigureBegin::Filled);		// top-left
				sink.addLine({ r.right - curlSize, r.top });					// top-right

				sink.addBezier(													// bot-right
					{
						{ r.right - curlSize         , r.top + getHeight(r) * 0.4f},
						{ r.right - curlSize  , r.top + getHeight(r) * 0.6f},
						{ r.right        , r.bottom }
					});


				sink.addLine({ r.left + curlSize, r.bottom });
				sink.addBezier(													// bot-right
					{
						{ r.left   , r.top + getHeight(r) * 0.6f},
						{ r.left   , r.top + getHeight(r) * 0.4f},
						{ r.left   , r.top }
					});

				sink.endFigure(FigureEnd::Closed);
				sink.close();

				g.fillGeometry(curlGeom, fillBrush);

				// highlight
				auto curlBrush = g.createLinearGradientBrush(
					{r.right, r.top},
					{r.left, r.bottom},
					Colors::TransparentWhite, Color{ 1.0f, 1.0f, 1.0f, 0.5f }
				);
				g.fillGeometry(curlGeom, curlBrush);
			}
#if 0
			// ── Small shadow beneath the curl ──
			{
				const float sh = curlSize * 0.12f;
				auto csGeom = g.getFactory().createPathGeometry();
				auto sink = csGeom.open();
				sink.beginFigure({r.right - curlSize, r.bottom}, FigureBegin::Filled);
				sink.addQuadraticBezier({{r.right - curlSize * 0.5f, r.bottom + sh * 0.4f},
				                         {r.right + sh * 0.2f, r.bottom - curlSize + sh}});
				sink.addLine({r.right, r.bottom - curlSize});
				sink.addQuadraticBezier({{r.right - curlSize * 0.4f, r.bottom - curlSize * 0.5f},
				                         {r.right - curlSize, r.bottom}});
				sink.endFigure(FigureEnd::Closed);
				sink.close();

				auto sBrush = g.createSolidColorBrush(Color{0.0f, 0.0f, 0.0f, 0.12f});
				g.fillGeometry(csGeom, sBrush);
			}
#endif
			// ── Draw text ──
			{
				const float margin = 6.0f;
				const Rect textRect{
					r.left + margin,
					r.top + margin,
					r.right - margin,
					r.bottom - curlSize - margin
				};

				auto textBrush = g.createSolidColorBrush(Color{0.1f, 0.1f, 0.1f, 1.0f});
				auto& richFormat = getRichTextFormat(g);
				g.drawRichTextU(richFormat, textRect, textBrush);
			}

			return ReturnCode::Ok;
		}

		return ReturnCode::NoSupport;
	}
};

namespace
{
auto r = gmpi::Register<PostItGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Post-It" name="Post-It" category="Sub-Controls">
    <GUI graphicsApi="GmpiGui">
        <Pin name="Text" datatype="string" default="Hello!"/>
    </GUI>
</Plugin>
)XML");
}

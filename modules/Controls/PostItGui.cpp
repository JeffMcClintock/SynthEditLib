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
		auto curlGeom = g.getFactory().createPathGeometry();
		auto sink = curlGeom.open();

		sink.beginFigure({ r.left, r.top }, FigureBegin::Filled);		// top-left
		sink.addLine({ r.right - curlSize, r.top });					// top-right

		sink.addBezier(													// bot-right
			{
				{ r.right - curlSize  , r.top + getHeight(r) * 0.4f},
				{ r.right - curlSize  , r.top + getHeight(r) * 0.6f},
				{ r.right             , r.bottom }
			});


		sink.addLine({ r.left + curlSize, r.bottom });
		sink.addBezier(													// bot-right
			{
				{ r.left , r.top + getHeight(r) * 0.6f},
				{ r.left , r.top + getHeight(r) * 0.4f},
				{ r.left , r.top }
			});

		sink.endFigure(FigureEnd::Closed);
		sink.close();

		return curlGeom;
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
		*returnRect = inflateRect(bounds, 500);
		//const auto ext = getShadowExtent();
		//returnRect->left -= ext;
		//returnRect->top -= ext;
		//returnRect->right += ext;
		//returnRect->bottom += ext;
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
		if(layer < -1 || layer > 0)
			return ReturnCode::NoSupport;

		Graphics g(drawingContext);
		const auto& r = bounds;
		const float w = getWidth(r);
		const float h = getHeight(r);
		const float curlSize = getCurlSize();

		if(layer == -1)
		{
			const auto blurRadius =static_cast<int>(0.06f * (std::min)(w, h));
			Rect shadowBounds{ 0 - blurRadius, 0 - blurRadius, w + blurRadius, h + blurRadius };

			shadowBlur.tint = Color{ 0.0f, 0.0f, 0.0f, 0.4f };
			shadowBlur.blurRadius = blurRadius;

			const auto width = (std::max)(1u, static_cast<uint32_t>(getWidth(bounds)));
			const auto height = (std::max)(1u, static_cast<uint32_t>(getHeight(bounds)));
			const SizeU shadowBitmapSize{ width + 2 * shadowBlur.blurRadius, height + 2 * shadowBlur.blurRadius };
			updateShadowCache(shadowBitmapSize);

			auto noteGeom = createNoteShape(g, r, curlSize);

			Graphics g(drawingContext);
			const auto orig = g.getTransform();
			g.setTransform(makeTranslation({ (float) -shadowBlur.blurRadius + getCurlSize(),(float)-shadowBlur.blurRadius + getCurlSize() }) * orig);
			auto rect = r;
			Rect maskRect
			{
				0,
				0,
				shadowBitmapSize.width,
				shadowBitmapSize.height,
			};
			shadowBlur.draw(g, maskRect, [&](Graphics& mask)
				{
					auto brush = mask.createSolidColorBrush(Colors::White);

					auto& r = shadowBlur.blurRadius;
					rect.right -= getCurlSize() * 2.0f;
//					rect.bottom += getCurlSize();
					const Rect shadowRect{
						rect.left + r + r / 2,
						rect.top + r,
						rect.right - r - r - r,
						rect.bottom  - r
					};
					mask.fillRectangle(offsetRect(rect, { (float)r, (float)r }), brush);

//					mask.clear(Colors::White);
				});
			g.setTransform(orig); 

		}
		else
		{
//			return ReturnCode::Ok;
			// ── Yellow palette ──
			const Color stickyTop = colorFromHex(0xFAE37D, 0.9f); // dirty yellow over glue
			const Color yellowTop = colorFromHex(0xFFE479, 0.9f);
			const Color yellowBottom = colorFromHex(0xFFED8B, 0.9f);

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
				g.fillGeometry(noteGeom, fillBrush);

				// highlight
				auto curlBrush = g.createLinearGradientBrush(
					{r.right, r.top},
					{r.left, r.bottom},
					Colors::TransparentWhite, Color{ 1.0f, 1.0f, 1.0f, 0.5f }
				);
				g.fillGeometry(noteGeom, curlBrush);
			}

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
        <Pin name="Text" datatype="string" default="Hello!" private="true"/>
    </GUI>
</Plugin>
)XML");
}

// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.

#include "helpers/GmpiPluginEditor.h"
#include "helpers/CachedBlur.h"
#include "helpers/ContextMenuHelper.h"
#include "../shared/it_enum_list.h"
#include <algorithm>
#include <cmath>
#include <string_view>

using namespace gmpi;
using namespace gmpi::drawing;
using namespace gmpi::editor;

namespace
{
enum class MenuItemType
{
	Normal,
	Separator,
	Break,
	SubMenu,
	SubMenuEnd,
};

std::string_view trimSpaces(std::string_view text)
{
	while(!text.empty() && text.front() == ' ')
	{
		text.remove_prefix(1);
	}

	while(!text.empty() && text.back() == ' ')
	{
		text.remove_suffix(1);
	}

	return text;
}

MenuItemType getMenuItemType(std::string_view text)
{
	text = trimSpaces(text);
	if(text.size() < 4)
	{
		return MenuItemType::Normal;
	}

	for(size_t i = 1; i < 4; ++i)
	{
		if(text[i] != text[0])
		{
			return MenuItemType::Normal;
		}
	}

	switch(text[0])
	{
	case '-':
		return MenuItemType::Separator;
	case '|':
		return MenuItemType::Break;
	case '>':
		return MenuItemType::SubMenu;
	case '<':
		return MenuItemType::SubMenuEnd;
	default:
		return MenuItemType::Normal;
	}
}

std::string menuText(std::string_view text)
{
	text = trimSpaces(text);
	if(text.size() >= 4)
	{
		const auto type = getMenuItemType(text);
		if(type == MenuItemType::SubMenu || type == MenuItemType::SubMenuEnd)
		{
			text.remove_prefix(4);
			text = trimSpaces(text);
		}
	}

	return std::string(text);
}
}

class KnobGui final : public PluginEditor
{
	Pin<float> pinValue;
	Pin<std::string> pinHint;
	Pin<std::string> pinMenuItems;
	Pin<int32_t> pinMenuSelection;
	Pin<bool> pinMouseDown;
	Pin<std::string> pinColor1;
	Pin<std::string> pinColor2;
	cachedBlur shadowBlur;
	SizeU shadowSize{};
	Point pointPrevious{};

	void redraw()
	{
		if(drawingHost)
		{
			Rect clipArea;
			getClipArea(&clipArea);
			drawingHost->invalidateRect(&clipArea);
		}
	}

	float getNormalizedValue() const
	{
		return (std::clamp)(pinValue.value, 0.0f, 1.0f);
	}

	Color getFillColor() const
	{
		return pinColor1.value.empty() ? colorFromHex(0x2E79C7u) : colorFromHexString(pinColor1.value);
	}

	Color getIndicatorColor() const
	{
		return pinColor2.value.empty() ? Color{ 1.0f, 1.0f, 1.0f, 0.94f } : colorFromHexString(pinColor2.value);
	}

	bool hasCapture() const
	{
		bool captured = false;
		if(inputHost.get())
		{
			inputHost->getCapture(captured);
		}
		return captured;
	}

	void updateShadowCache(const SizeU& size)
	{
		if(shadowSize.width != size.width || shadowSize.height != size.height)
		{
			shadowBlur.invalidate();
			shadowSize = size;
		}
	}

	Rect getLocalBounds(const SizeU& size) const
	{
		return { 0.0f, 0.0f, static_cast<float>(size.width), static_cast<float>(size.height) };
	}

	float getShadowExtent() const
	{
		const float radius = 0.5f * (std::min)(getWidth(bounds), getHeight(bounds));
		const float blurRadius = static_cast<float>((std::max)(1, static_cast<int>(std::ceil(radius * 0.1f))));
		const float blurOffset = 0.4f * radius;
		return blurOffset + blurRadius;
	}

	ReturnCode drawKnob(Graphics& g, const Rect& localBounds)
	{
		auto center = getCenter(localBounds);
		const float radius = 0.5f * (std::min)(getWidth(localBounds), getHeight(localBounds));
		const float bevelWidth = (std::max)(1.0f, radius * 0.12f);

		// Shadow
		{
			shadowBlur.tint = Color{ 0.0f, 0.0f, 0.0f, 0.6f }; // how dark the shadow is.
			shadowBlur.blurRadius = (std::max)(1, static_cast<int>(std::ceil(radius * 0.1f)));
			const float blurOffset = 0.4f * radius;

			const auto orig = g.getTransform();
			g.setTransform(makeTranslation({ blurOffset, blurOffset }) * orig);
			shadowBlur.draw(g, localBounds, [&](Graphics& mask)
				{
					auto brush = mask.createSolidColorBrush(Colors::White);
					mask.fillCircle(center, radius - shadowBlur.blurRadius, brush);
				});
			g.setTransform(orig);
		}

		// Fill
		{
			const auto fillColor = getFillColor();
			auto fillBrush = g.createRadialGradientBrush(
				{ center.x - radius, center.y - radius },
				radius * 4.f,
				interpolateColor(fillColor, Colors::White, 0.45f),
				interpolateColor(fillColor, Colors::Black, 0.10f)
			);
			g.fillCircle(center, radius, fillBrush);
		}

		// indicator
		{
			constexpr float kPi = 3.14159265f;
			const float normalized = getNormalizedValue();
			const float angle = (135.0f + 270.0f * normalized) * (kPi / 180.0f);
			const float cosA = std::cos(angle);
			const float sinA = std::sin(angle);
			const float ht = (std::min)((std::max)(1.0f, radius * 0.16f), radius * 0.95f);
			const float innerR = radius * 0.1f;

			// outer arc endpoints on the circle
			const float outerOffset = std::asin(ht / radius);
			Point outerB{
				center.x + radius * std::cos(angle - outerOffset),
				center.y + radius * std::sin(angle - outerOffset)
			};
			Point outerA{
				center.x + radius * std::cos(angle + outerOffset),
				center.y + radius * std::sin(angle + outerOffset)
			};

			// inner endpoints (flat)
			Point innerA{
				center.x + innerR * cosA - ht * sinA,
				center.y + innerR * sinA + ht * cosA
			};
			Point innerB{
				center.x + innerR * cosA + ht * sinA,
				center.y + innerR * sinA - ht * cosA
			};

			auto geometry = g.getFactory().createPathGeometry();
			auto sink = geometry.open();
			sink.beginFigure(innerB, FigureBegin::Filled);
			sink.addLine(outerB);
			sink.addArc({ outerA, { radius, radius }, 0.f, SweepDirection::Clockwise, ArcSize::Small });
			sink.addLine(innerA);
			sink.endFigure();
			sink.close();

			auto indicatorBrush = g.createSolidColorBrush(getIndicatorColor());
			g.fillGeometry(geometry, indicatorBrush);
		}

		// Bevel ring arround edge
		{
			const auto bevelRadius = radius - 0.5f * bevelWidth;
			auto bevelBrush = g.createLinearGradientBrush(
				{ center.x - radius, center.y - radius },
				{ center.x + radius, center.y + radius },
				Color{ 0.85f, 0.85f, 0.85f, 0.55f },
				Color{ 0.20f, 0.20f, 0.20f, 0.55f }
			);
			g.drawCircle(center, bevelRadius, bevelBrush, bevelWidth);
		}
		return ReturnCode::Ok;
	}

public:
	KnobGui()
	{
		pinValue.onUpdate = [this](PinBase*) { redraw(); };
		pinColor1.onUpdate = [this](PinBase*) { redraw(); };
		pinColor2.onUpdate = [this](PinBase*) { redraw(); };
	}

	ReturnCode getClipArea(Rect* returnRect) override
	{
		*returnRect = bounds;
		const auto shadowExtent = getShadowExtent();
		returnRect->right += shadowExtent;
		returnRect->bottom += shadowExtent;
		return ReturnCode::Ok;
	}

	// todo: render in layers. shadow in layer -1, knob in layer 0.
	ReturnCode render(drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);
		const auto width = (std::max)(1u, static_cast<uint32_t>(getWidth(bounds)));
		const auto height = (std::max)(1u, static_cast<uint32_t>(getHeight(bounds)));
		const SizeU size{ width, height };
		updateShadowCache(size);
		return drawKnob(g, getLocalBounds(size));
	}

	ReturnCode onPointerDown(Point point, int32_t flags) override
	{
		if((flags & static_cast<int32_t>(gmpi::api::GG_POINTER_FLAG_FIRSTBUTTON)) == 0)
			return ReturnCode::Ok;

		pointPrevious = point;
		pinMouseDown = true;

		if(inputHost.get())
			inputHost->setCapture();

		return ReturnCode::Ok;
	}

	ReturnCode onPointerMove(Point point, int32_t flags) override
	{
		if(!hasCapture())
			return ReturnCode::Unhandled;

		const float coarseness = (flags & static_cast<int32_t>(gmpi::api::GG_POINTER_KEY_CONTROL)) != 0 ? 0.001f : 0.005f;
		auto newValue = pinValue.value - coarseness * (point.y - pointPrevious.y);
		newValue = (std::clamp)(newValue, 0.0f, 1.0f);
		pointPrevious = point;

		pinValue = newValue;
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

	gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point point, gmpi::api::IUnknown* contextMenuItemsSink) override
	{
		if(pinMenuItems.value.empty())
			return ReturnCode::Unhandled;

		gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
		unknown = contextMenuItemsSink;
		auto sink = unknown.as<gmpi::api::IContextItemSink>();
		if(!sink)
			return ReturnCode::Fail;

		ContextMenuHelper menu(sink.get());
		for(const auto& item : it_enum_list2(pinMenuItems.value))
		{
			switch(getMenuItemType(item.text))
			{
			case MenuItemType::Separator:
				menu.addSeparator();
				break;

			case MenuItemType::SubMenu:
				menu.beginSubMenu(menuText(item.text).c_str());
				break;

			case MenuItemType::SubMenuEnd:
				menu.endSubMenu();
				break;

			case MenuItemType::Break:
				break;

			case MenuItemType::Normal:
				menu.addItem(menuText(item.text).c_str(), item.id);
				break;
			}
		}

		return ReturnCode::Ok;
	}

	gmpi::ReturnCode onContextMenu(int32_t idx) override
	{
		pinMenuSelection = idx;
		pinMenuSelection = -1;
		return ReturnCode::Ok;
	}
};

namespace
{
auto r = gmpi::Register<KnobGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Knob" name="Knob" category="Sub-Controls">
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

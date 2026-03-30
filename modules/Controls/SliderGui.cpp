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

class SliderGui final : public PluginEditor
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
		assert(inputHost.get());

		bool captured = false;
		inputHost->getCapture(captured);
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

	Rect getHandleAndShadowRect(const Rect& localBounds) const
	{
		const auto handleRect = getHandleRect(localBounds);
		const float radius = (std::max)(1.0f, getHeight(handleRect) * 0.5f);
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
		const float handleHeight = (std::max)(12.0f, width * 0.5f);
		const float margin = handleHeight * 0.5f;
		const float trackTop = localBounds.top + margin;
		const float trackBottom = localBounds.bottom - margin;
		const float trackRange = trackBottom - trackTop;
		const float normalized = getNormalizedValue();

		// value 1.0 = top, value 0.0 = bottom
		const float handleCenterY = trackBottom - normalized * trackRange;

		return {
			localBounds.left,
			handleCenterY - handleHeight * 0.5f,
			localBounds.right,
			handleCenterY + handleHeight * 0.5f
		};
	}

	float valueFromPoint(const Rect& localBounds, Point point) const
	{
		const float height = getHeight(localBounds);
		const float handleHeight = (std::max)(12.0f, height * 0.12f);
		const float margin = handleHeight * 0.5f;
		const float trackTop = localBounds.top + margin;
		const float trackBottom = localBounds.bottom - margin;
		const float trackRange = trackBottom - trackTop;

		if(trackRange <= 0.0f)
			return 0.0f;

		// invert: top = 1.0, bottom = 0.0
		return (std::clamp)(1.0f - (point.y - trackTop) / trackRange, 0.0f, 1.0f);
	}

	ReturnCode drawSlider(Graphics& g, const Rect& localBounds)
	{
		const float width = getWidth(localBounds);
		const float height = getHeight(localBounds);
		const float handleHeight = (std::max)(12.0f, height * 0.12f);
		const float cornerRadius = width * 0.5f;

		// Track background
		{
			const float trackWidth = (std::max)(4.0f, width * 0.25f);
			const float trackLeft = localBounds.left + (width - trackWidth) * 0.5f;
			const float trackRadius = trackWidth * 0.5f;

			auto trackBrush = g.createSolidColorBrush(Color{ 0.0f, 0.0f, 0.0f, 0.15f });
			g.fillRoundedRectangle(
				{ { trackLeft, localBounds.top, trackLeft + trackWidth, localBounds.bottom }, trackRadius, trackRadius },
				trackBrush
			);
		}

		const auto handleRect = getHandleRect(localBounds);
		const float radius = (std::max)(1.0f, getHeight(handleRect) * 0.5f); // equivalent of knob radius
		const float bevelWidth = (std::max)(1.0f, radius * 0.12f);

		// Handle shadow
		{
			shadowBlur.tint = Color{ 0.0f, 0.0f, 0.0f, 0.6f };
			shadowBlur.blurRadius = (std::max)(1, static_cast<int>(std::ceil(radius * 0.1f)));
			const float blurOffset = 0.4f * radius;

			const auto orig = g.getTransform();
			g.setTransform(makeTranslation({ blurOffset, handleRect.top + blurOffset}) * orig);
			gmpi::drawing::Rect rectAtOrigin{ 0.0f, 0.0f, getWidth(handleRect), getHeight(handleRect) };
			shadowBlur.draw(g, rectAtOrigin, [&](Graphics& mask)
				{
					auto shadowRect = inflateRect(rectAtOrigin, -shadowBlur.blurRadius);
					auto brush = mask.createSolidColorBrush(Colors::White);
					mask.fillRoundedRectangle(
						{ shadowRect, radius, radius },
						brush
					);
				});
			g.setTransform(orig);
		}

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
			const float centerY = (handleRect.top + handleRect.bottom) * 0.5f;
			const float ht = (std::min)((std::max)(1.0f, width * 0.05f), radius * 0.95f);
			const float d = std::sqrt(radius * radius - ht * ht);

			const float leftX = handleRect.left + radius - d;
			const float rightX = handleRect.right - radius + d;

			auto geometry = g.getFactory().createPathGeometry();
			auto sink = geometry.open();
			sink.beginFigure({ leftX, centerY - ht }, FigureBegin::Filled);
			sink.addLine({ rightX, centerY - ht });
			sink.addArc({ { rightX, centerY + ht }, { radius, radius }, 0.f, SweepDirection::Clockwise, ArcSize::Small });
			sink.addLine({ leftX, centerY + ht });
			sink.addArc({ { leftX, centerY - ht }, { radius, radius }, 0.f, SweepDirection::Clockwise, ArcSize::Small });
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
	SliderGui()
	{
		pinValue.onUpdate = [this](PinBase*) { redraw(); };
		pinColor1.onUpdate = [this](PinBase*) { redraw(); };
		pinColor2.onUpdate = [this](PinBase*) { redraw(); };
	}

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

	ReturnCode render(drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);
		const auto width = (std::max)(1u, static_cast<uint32_t>(getWidth(bounds)));
		const auto height = (std::max)(1u, static_cast<uint32_t>(getHeight(bounds)));
		const SizeU size{ width, height };
		updateShadowCache(size);
		return drawSlider(g, getLocalBounds(size));
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
			auto newValue = pinValue.value - coarseness * (point.y - pointPrevious.y);
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
auto r = gmpi::Register<SliderGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE VSlider" name="Slider" category="Sub-Controls">
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

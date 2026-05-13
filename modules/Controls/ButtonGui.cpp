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
Color withAlpha(Color color, float alpha)
{
	color.a = alpha;
	return color;
}

enum class MenuItemType { Normal, Separator, Break, SubMenu, SubMenuEnd };

std::string_view trimSpaces(std::string_view text)
{
	while(!text.empty() && text.front() == ' ') text.remove_prefix(1);
	while(!text.empty() && text.back()  == ' ') text.remove_suffix(1);
	return text;
}

MenuItemType menuItemType(std::string_view text)
{
	text = trimSpaces(text);
	if(text.size() < 4) return MenuItemType::Normal;
	for(size_t i = 1; i < 4; ++i)
		if(text[i] != text[0]) return MenuItemType::Normal;
	switch(text[0])
	{
	case '-': return MenuItemType::Separator;
	case '|': return MenuItemType::Break;
	case '>': return MenuItemType::SubMenu;
	case '<': return MenuItemType::SubMenuEnd;
	default:  return MenuItemType::Normal;
	}
}

std::string menuText(std::string_view text)
{
	text = trimSpaces(text);
	if(text.size() >= 4)
	{
		const auto t = menuItemType(text);
		if(t == MenuItemType::SubMenu || t == MenuItemType::SubMenuEnd)
		{
			text.remove_prefix(4);
			text = trimSpaces(text);
		}
	}
	return std::string(text);
}
}

class ButtonGui final : public PluginEditor, public gmpi::api::IDrawingLayer
{
	Pin<bool> pinValue;
	Pin<std::string> pinMenuItems;
	Pin<int32_t> pinMenuSelection;
	Pin<bool> pinMouseDown;
	Pin<std::string> pinHint;
	Pin<bool> pinToggle;
	Pin<std::string> pinColor1;
	Pin<std::string> pinColor2;
	Pin<float> pinRadius;
	cachedBlur shadowBlur;
	SizeU shadowSize{};

	void redraw()
	{
		if(drawingHost)
		{
			Rect clipArea;
			getClipArea(&clipArea);
			drawingHost->invalidateRect(&clipArea);
		}
	}

	bool isPressed() const
	{
		return pinValue.value;
	}

	Color getFillColor() const
	{
		return pinColor1.value.empty() ? colorFromHex(0x2E79C7u) : colorFromHexString(pinColor1.value);
	}

	Color getHighlightColor() const
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
		const float minDimension = (std::min)(getWidth(bounds), getHeight(bounds));
		const float blurRadius = static_cast<float>((std::max)(1, static_cast<int>(std::ceil(minDimension * 0.08f))));
		const float blurOffset = 0.16f * minDimension;
		return blurOffset + blurRadius;
	}

public:
	ButtonGui()
	{
		pinValue.onUpdate = [this](PinBase*) { redraw(); };
		pinColor1.onUpdate = [this](PinBase*) { redraw(); };
		pinColor2.onUpdate = [this](PinBase*) { redraw(); };
		pinRadius.onUpdate = [this](PinBase*) { redraw(); };
	}

	ReturnCode getClipArea(Rect* returnRect) override
	{
		*returnRect = bounds;
		const auto shadowExtent = getShadowExtent();
		returnRect->left -= shadowExtent;
		returnRect->top -= shadowExtent;
		returnRect->right += shadowExtent;
		returnRect->bottom += shadowExtent;
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
		Graphics g(drawingContext);
		const auto width = (std::max)(1u, static_cast<uint32_t>(getWidth(bounds)));
		const auto height = (std::max)(1u, static_cast<uint32_t>(getHeight(bounds)));
		const SizeU size{ width, height };
		const Rect localBounds = getLocalBounds(size);
		const auto widthF = getWidth(localBounds);
		const auto heightF = getHeight(localBounds);
		const float minDimension = (std::min)(widthF, heightF);
		const float radiusFraction = (std::clamp)(pinRadius.value, 0.0f, 1.0f);
		const float cornerRadius = (std::max)(2.0f, minDimension * radiusFraction);
		const bool pressed = isPressed();

		if (layer == -1)
		{
			updateShadowCache(size);

			// Draw shadow
			const float blurOffset = 0.16f * minDimension;
			const float shadowDirection = pressed ? 0.0f : 1.0f;
			const RoundedRect buttonRect(inflateRect(localBounds, -0.5f * blurOffset), cornerRadius, cornerRadius);

			shadowBlur.tint = Color{ 0.0f, 0.0f, 0.0f, 0.6f };
			shadowBlur.blurRadius = (std::max)(1, static_cast<int>(std::ceil(minDimension * 0.08f)));

			const auto orig = g.getTransform();
			g.setTransform(makeTranslation({ shadowDirection * blurOffset, shadowDirection * blurOffset }) * orig);
			shadowBlur.draw(g, localBounds, [&](Graphics& mask)
				{
					auto brush = mask.createSolidColorBrush(Colors::White);
					mask.fillRoundedRectangle(buttonRect, brush);
				});
			g.setTransform(orig);

			return ReturnCode::Ok;
		}

		if (layer == 0)
		{
			// Draw button
			const float bevelWidth = (std::max)(1.0f, minDimension * 0.08f);
			const Rect bodyRect = localBounds;
			const RoundedRect buttonRect(bodyRect, cornerRadius, cornerRadius);

			// Fill
			{
				const auto fillColor = getFillColor();
				auto fillBrush = g.createRadialGradientBrush(
					{ bodyRect.left, bodyRect.top },
					(std::max)(widthF, heightF) * 1.8f,
					interpolateColor(fillColor, Colors::White, pressed ? 0.05f : 0.15f),
					interpolateColor(fillColor, Colors::Black, pressed ? 0.95f : 0.90f)
				);
				g.fillRoundedRectangle(buttonRect, fillBrush);
			}

			// Bevel ring around edge
			{
				const float bevelCornerRadius = (std::max)(0.0f, cornerRadius - 0.5f * bevelWidth);
				const RoundedRect bevelRect(inflateRect(bodyRect, -0.5f * bevelWidth), bevelCornerRadius, bevelCornerRadius);

				auto bevelBrush = g.createLinearGradientBrush(
					{ bevelRect.rect.left, bevelRect.rect.top },
					{ bevelRect.rect.right, bevelRect.rect.bottom },
					Color{ 1,1,1, 0.15f },
					Color{ 0,0,0, 0.15f }
				);
				g.drawRoundedRectangle(bevelRect, bevelBrush, bevelWidth);
			}

			return ReturnCode::Ok;
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

	ReturnCode populateContextMenu(Point /*point*/, gmpi::api::IUnknown* contextMenuItemsSink) override
	{
		if(pinMenuItems.value.empty())
			return ReturnCode::Unhandled;

		gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
		unknown = contextMenuItemsSink;
		auto sink = unknown.as<gmpi::api::IContextItemSink>();
		if(!sink)
			return ReturnCode::Fail;

		ContextMenuHelper menu(sink.get());

		menu.currentCallback =
			[this](int32_t selectedId)
			{
				pinMenuSelection = selectedId;
				pinMenuSelection = -1;
			};

		for(const auto& item : it_enum_list2(pinMenuItems.value))
		{
			switch(menuItemType(item.text))
			{
			case MenuItemType::Separator:  menu.addSeparator();                            break;
			case MenuItemType::SubMenu:    menu.beginSubMenu(menuText(item.text).c_str()); break;
			case MenuItemType::SubMenuEnd: menu.endSubMenu();                              break;
			case MenuItemType::Break:                                                      break;
			case MenuItemType::Normal:     menu.addItem(menuText(item.text).c_str(), item.id); break;
			}
		}
		return ReturnCode::Ok;
	}

	ReturnCode onPointerDown(Point, int32_t flags) override
	{
		if((flags & static_cast<int32_t>(gmpi::api::PointerFlags::FirstButton)) == 0)
			return ReturnCode::Unhandled;

		if(inputHost.get())
			inputHost->setCapture();

		pinMouseDown = true;
		pinValue = pinToggle.value ? !pinValue.value : true;

		return ReturnCode::Handled; // Swallow click — prevents enclosing module's double-click "open structure" handler.
	}

	ReturnCode onPointerMove(Point, int32_t) override
	{
		if(!hasCapture())
			return ReturnCode::Unhandled;

		return ReturnCode::Ok;
	}

	ReturnCode onPointerUp(Point, int32_t) override
	{
		if(!hasCapture())
			return ReturnCode::Unhandled;

		assert(inputHost.get());
		inputHost->releaseCapture();

		pinMouseDown = false;
		pinValue = pinToggle.value ? pinValue.value: false;

		return ReturnCode::Ok;
	}
};

namespace
{
auto r = gmpi::Register<ButtonGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Button" name="Button" category="Sub-Controls">
	<GUI>
		<Pin name="Value" datatype="bool"/>
		<Pin name="Menu Items" datatype="string"/>
		<Pin name="Menu Selection" datatype="int"/>
		<Pin name="Mouse Down" datatype="bool"/>
		<Pin name="Hint" datatype="string"/>
		<Pin name="Toggle" datatype="bool"/>
		<Pin name="Base Color" datatype="string" default="2E79C7"/>
		<Pin name="Line Color" datatype="string" default="EEEEEE"/>
		<Pin name="Radius" datatype="float" default="0.24"/>
	</GUI>
</Plugin>
)XML");
}

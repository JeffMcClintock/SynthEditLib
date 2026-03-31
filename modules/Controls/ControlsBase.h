// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
#pragma once

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

// ── Base for all visual controls ─────────────────────────────────────────────
class ControlsBase : public PluginEditor
{
protected:
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

	bool hasCapture() const
	{
		bool captured = false;
		if(inputHost.get())
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

	ReturnCode measure(const gmpi::drawing::Size* availableSize, gmpi::drawing::Size* returnDesiredSize) override
	{
		*returnDesiredSize = *availableSize;

		// impose a minimum size
		returnDesiredSize->width = (std::max)(4.0f, returnDesiredSize->width);
		returnDesiredSize->height = (std::max)(4.0f, returnDesiredSize->height);

		return ReturnCode::Ok;
	}
};

// ── Base for float-value controls with context menus (Knob, Slider, HSlider) ─
class ValueControlBase : public ControlsBase
{
protected:
	Pin<float>       pinValue;
	Pin<std::string> pinHint;
	Pin<std::string> pinMenuItems;
	Pin<int32_t>     pinMenuSelection;
	Pin<bool>        pinMouseDown;
	Pin<std::string> pinColor1;
	Pin<std::string> pinColor2;
	Point            pointPrevious{};

	ValueControlBase()
	{
		pinValue.onUpdate  = [this](PinBase*) { redraw(); };
		pinColor1.onUpdate = [this](PinBase*) { redraw(); };
		pinColor2.onUpdate = [this](PinBase*) { redraw(); };
	}

	Color getFillColor() const
	{
		return pinColor1.value.empty() ? colorFromHex(0x2E79C7u) : colorFromHexString(pinColor1.value);
	}

	Color getIndicatorColor() const
	{
		return pinColor2.value.empty() ? Color{ 1.0f, 1.0f, 1.0f, 0.94f } : colorFromHexString(pinColor2.value);
	}

	float getNormalizedValue() const
	{
		return (std::clamp)(pinValue.value, 0.0f, 1.0f);
	}

	// ── Context menu ─────────────────────────────────────────────────────────
	gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point /*point*/, gmpi::api::IUnknown* contextMenuItemsSink) override
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
			switch(menuItemType(item.text))
			{
			case MenuItemType::Separator:  menu.addSeparator();                         break;
			case MenuItemType::SubMenu:    menu.beginSubMenu(menuText(item.text).c_str()); break;
			case MenuItemType::SubMenuEnd: menu.endSubMenu();                            break;
			case MenuItemType::Break:                                                    break;
			case MenuItemType::Normal:     menu.addItem(menuText(item.text).c_str(), item.id); break;
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

private:
	enum class MenuItemType { Normal, Separator, Break, SubMenu, SubMenuEnd };

	static std::string_view trimSpaces(std::string_view text)
	{
		while(!text.empty() && text.front() == ' ') text.remove_prefix(1);
		while(!text.empty() && text.back()  == ' ') text.remove_suffix(1);
		return text;
	}

	static MenuItemType menuItemType(std::string_view text)
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

	static std::string menuText(std::string_view text)
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
};

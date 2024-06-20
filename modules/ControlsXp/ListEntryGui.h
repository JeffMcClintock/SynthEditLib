#ifndef LISTENTRYGUI_H_INCLUDED
#define LISTENTRYGUI_H_INCLUDED

#include "ClassicControlGuiBase.h"

class ListEntryGui : public ClassicControlGuiBase
{
#ifdef _DEBUG
	bool ismeasured;
#endif
	bool isarranged;

	enum CComboMode { ACM_PLAIN, ACM_LED_STACK, ACM_LED_STACK_LABELED, ACM_BUTTON_SELECTOR, ACM_BUTTON_STACK, ACM_ROTARY_SWITCH_LABELED, ACM_ROTARY_SWITCH, ACM_UP_DOWN_SELECTOR };

	int currentAppearance;

public:
	ListEntryGui();

	// IMpGraphics overrides.
	int32_t onPointerUp(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
	int32_t initialize() override;
	int32_t measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE* returnDesiredSize) override;
	int32_t arrange(GmpiDrawing_API::MP1_RECT finalRect) override;
	int32_t populateContextMenu(float x, float y, gmpi::IMpUnknown* contextMenuItemsSink) override;
	int32_t onContextMenu(int32_t selection) override;

private:
	void OnWidgetUpdate(int32_t newvalue);

	void onSetItems();
	void onSetValueIn();
	void onSetAppearance();
	void Increment(int inc = 1);

	IntGuiPin pinValueIn;
	StringGuiPin pinItemList;
	IntGuiPin pinAppearance;
	// context menu.
	StringGuiPin pinMenuItems;
	IntGuiPin pinMenuSelection;

	int listEntryCount;
	const int rowSpacing = 4; // from CGroup::m_col_spacing
};

#endif



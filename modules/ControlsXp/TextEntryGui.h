#ifndef TEXTENTRYGUI_H_INCLUDED
#define TEXTENTRYGUI_H_INCLUDED

#include "ClassicControlGuiBase.h"
#include "../se_sdk3/mp_gui.h"

class TextEntryGui : public ClassicControlGuiBase
{
public:
	TextEntryGui();

	int32_t arrange(GmpiDrawing_API::MP1_RECT finalRect) override;
	int32_t measure(GmpiDrawing_API::MP1_SIZE availableSize, GmpiDrawing_API::MP1_SIZE* returnDesiredSize) override;
	/*
	// overrides.
	int32_t onPointerUp(int32_t flags, GmpiDrawing_API::MP1_POINT point) override;
	int32_t initialize() override;
	*/

private:
	std::string getDefaultFolder(std::wstring extension);
	void OnWidgetUpdate(const std::string& newvalue);
	void OnBrowseButton(float newvalue);
	void onSetpatchValue();
 	void onSetExtension();
	void OnPopupmenuComplete(int32_t result);

 	StringGuiPin pinpatchValue;
	StringGuiPin pinExtension;

	float browseButtonState;
	GmpiGui::FileDialog nativeFileDialog;
};

#endif



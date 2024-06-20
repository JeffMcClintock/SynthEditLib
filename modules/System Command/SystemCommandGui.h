#ifndef SYSTEMCOMMANDGUI_H_INCLUDED
#define SYSTEMCOMMANDGUI_H_INCLUDED

#include "mp_sdk_gui.h"

class SystemCommandGui : public MpGuiBase
{
public:
	SystemCommandGui( IMpUnknown* host );

	// overrides
	int32_t initialize();

private:
 	void onSetTrigger();

	bool previousTrigger;

 	BoolGuiPin trigger;
 	IntGuiPin command;
 	StringGuiPin commandList;
 	StringGuiPin filename;
};

#endif



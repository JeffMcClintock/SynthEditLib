#ifndef BOOLSTOINTGUI_H_INCLUDED
#define BOOLSTOINTGUI_H_INCLUDED

#include "mp_sdk_gui.h"
#include <vector>

class CommandTriggerGui : public MpGuiBase
{
public:
	CommandTriggerGui( IMpUnknown* host );

	int32_t setPin( int32_t pinId, int32_t voice, int32_t size, void* data );
	int32_t initialize();

private:
	IntGuiPin value;

	std::vector<bool> previousValues;
	int outputPinCount_;
};

#endif



// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.

#include "Processor.h"

using namespace gmpi;

struct TextEntry final : public Processor
{
	StringInPin pinpatchValue;
	StringOutPin pinTextOut;

	void onSetPins() override
	{
		if (pinpatchValue.isUpdated())
			pinTextOut = pinpatchValue;
	}
};

// Registration is handled by TextEntryGui.cpp's XML which includes both <Audio> and <GUI> sections.
namespace
{
auto r = Register<TextEntry>::withId("SE Text EntryG");
}

// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
#include "Processor.h"

using namespace gmpi;

struct MIDIActivity final : public Processor
{
	MidiOutPin pinMIDIData;
	IntOutPin pinActivity;
	BoolInPin pinMPEMode;

	MIDIActivity()
	{
	}

	void onSetPins() override
	{
		// Check which pins are updated.
		if( pinMPEMode.isUpdated() )
		{
		}
	}
};

namespace
{
auto r = Register<MIDIActivity>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE MIDI Activity" name="MIDI Activity" category="MIDI">
    <Parameters>
        <Parameter id="0" datatype="int" name="" persistant="false"/>
    </Parameters>
    <Audio>
        <Pin name="MIDI Data" datatype="midi" direction="out"/>
        <Pin name="Activity" datatype="int" direction="out" private="true" parameterId="0"/>
        <Pin name="MPE Mode" datatype="bool"/>
    </Audio>
</Plugin>
)XML");
}

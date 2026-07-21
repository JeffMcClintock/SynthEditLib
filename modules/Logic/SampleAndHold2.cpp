// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
#include "Processor.h"

using namespace gmpi;

struct SampleAndHold2 final : public Processor
{
	AudioInPin pinInput;
	BoolInPin pinHold;
	AudioOutPin pinOutput;

	float holdValue{};
	SampleAndHold2() = default;

	void subProcess( int sampleFrames )
	{
		auto output = getBuffer(pinOutput);

		for( int s = sampleFrames; s > 0; --s )
			*output++ = holdValue;
	}

	void onSetPins() override
	{
		if(pinHold.isUpdated() && pinHold.getValue())
			holdValue = pinInput.getValue();

		// Set state of output audio pins.
		pinOutput.setStreaming(false);

		// Set processing method.
		setSubProcess(&SampleAndHold2::subProcess);
	}
};

namespace
{
auto r = Register<SampleAndHold2>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Sample And Hold2" name="Sample and Hold2" category="Logic">
    <Audio>
        <Pin name="Audio" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Hold" datatype="bool" />
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");
}

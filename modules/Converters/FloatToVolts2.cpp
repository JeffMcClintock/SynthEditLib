/* Copyright (c) 2007-2025 SynthEdit Ltd

Permission to use, copy, modify, and /or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS.IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
#include "Processor.h"

using namespace gmpi;

struct FloatToVolts2 final : public Processor
{
	IntInPin pinSmoothing;
	FloatInPin pinFloatIn;
	AudioOutPin pinVoltsOut;

	FloatToVolts2()
	{
	}

	void subProcess( int sampleFrames )
	{
		// get pointers to in/output buffers.
		auto voltsOut = getBuffer(pinVoltsOut);

		for( int s = sampleFrames; s > 0; --s )
		{
			// TODO: Signal processing goes here.

			// Increment buffer pointers.
			++voltsOut;
		}
	}

	void onSetPins() override
	{
		// Check which pins are updated.
		if( pinSmoothing.isUpdated() )
		{
		}
		if( pinFloatIn.isUpdated() )
		{
		}

		// Set state of output audio pins.
		pinVoltsOut.setStreaming(true);

		// Set processing method.
		setSubProcess(&FloatToVolts2::subProcess);
	}
};

namespace
{
auto r = Register<FloatToVolts2>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<PluginList>
    <Plugin id="SE Float to Volts2" name="Float to Volts2" category="Conversion">
        <Audio>
            <Pin name="Smoothing" datatype="enum" default="2" metadata="None,Fast (4 samp),Smooth (30ms)"/>
            <Pin name="Float In" datatype="float"/>
            <Pin name="Volts Out" datatype="float" rate="audio" direction="out"/>
        </Audio>
    </Plugin>
</PluginList>
)XML");
}

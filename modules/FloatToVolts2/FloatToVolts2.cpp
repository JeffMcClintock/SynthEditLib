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
#include "..\se_sdk3\smart_audio_pin.h" // for the RampGeneratorAdaptive

using namespace gmpi;

struct FloatToVolts2 final : public Processor
{
	gmpi::IntInPin pinSmoothing;
	gmpi::FloatInPin pinFloatIn;
	gmpi::AudioOutPin pinVoltsOut;

	RampGeneratorAdaptive smoother;
	float smoothTimeSamples{ -1 };

	FloatToVolts2() = default;

	gmpi::ReturnCode open(api::IUnknown* phost) override
	{
		auto r = Processor::open(phost);

		smoother.Init(host->getSampleRate());

		return r;
	}

	void subProcess( int sampleFrames )
	{
		if (smoother.isDone())
		{
			pinVoltsOut.setStreaming(false, 0);
		}

		auto voltsOut = getBuffer(pinVoltsOut);

		for( int s = sampleFrames; s > 0; --s )
		{
			*voltsOut++ = smoother.getNext();
		}
	}

	void onSetPins() override
	{
		if(pinSmoothing.isUpdated() )
		{
			const auto target = 0.1f * pinFloatIn.getValue();
			switch (pinSmoothing.getValue())
			{
			default:
				smoothTimeSamples = -1; // N/A
				break;

			case 1: // Fast
				smoothTimeSamples = 4.0f;
				break;

			case 2: // Smooth
				smoothTimeSamples = 0.03f * host->getSampleRate();
				break;

			case 3: // None
				smoothTimeSamples = 0.0f; // N/A
				break;
			}
		}
		if (pinFloatIn.isUpdated())
		{
			const auto target = 0.1f * pinFloatIn.getValue();
			switch (pinSmoothing.getValue())
			{
			default:
				smoother.setTarget(target);
				break;

			case 1: // Fast
			case 2: // Smooth
				smoother.SetTargetWithTimeInSamples(target, smoothTimeSamples);
				break;

			case 3: // None
				smoother.setValueInstant(target);
				break;
			}
		}

		// Set state of output audio pins.
		pinVoltsOut.setStreaming(true);

		// Set processing method.
		setSubProcess(&FloatToVolts2::subProcess);
	}
};

namespace
{
auto r = gmpi::Register<FloatToVolts2>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<PluginList>
    <Plugin id="SE Float to Volts2" name="Float to Volts2" category="Debug">
        <Audio>
            <Pin name="Smoothing" datatype="enum" metadata="Auto, Fast (4 samp), Smooth (30ms), None"/>
            <Pin name="Float In" datatype="float"/>
            <Pin name="Volts Out" datatype="float" rate="audio" direction="out"/>
        </Audio>
    </Plugin>
</PluginList>
)XML");
}

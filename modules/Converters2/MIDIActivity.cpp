// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
#include "Processor.h"

using namespace gmpi;

// Indicates MIDI traffic. Any incoming message sets the 'Activity' pin high,
// it falls again 0.5ms later. Sleep is inhibited while the pulse is running,
// else the module would nod off before the pin could be set low again.
struct MIDIActivity final : public Processor
{
	MidiInPin pinMIDIData;
	BoolOutPin pinActivity;

	int pulseSamples = 0;	// duration of the activity pulse.
	int countdown = 0;		// samples remaining until 'Activity' goes low.

	MIDIActivity() = default;

	gmpi::ReturnCode open(api::IUnknown* phost) override
	{
		const auto r = Processor::open(phost);

		if (r != ReturnCode::Ok)
			return r;

		pulseSamples = (std::max)(1, static_cast<int>(0.0005f * host->getSampleRate())); // 0.5 ms.

		return r;
	}

	void onMidiMessage(int /*pin*/, std::span<const uint8_t> /*midiMessage*/) override
	{
		pinActivity.setValue(true); // block position is exact while handling an event.

		countdown = pulseSamples;

		// stay awake long enough to time the pulse out.
		setSleep(false);
		setSubProcess(&MIDIActivity::subProcess);
	}

	void subProcess(int sampleFrames)
	{
		if (countdown >= sampleFrames) // pulse extends past this chunk.
		{
			countdown -= sampleFrames;
			return;
		}

		pinActivity.setValue(false, getBlockPosition() + countdown);
		countdown = 0;

		// nothing to do until the next MIDI message arrives.
		setSleep(true);
		setSubProcess(&MIDIActivity::subProcessNothing);
	}
};

namespace
{
auto r = Register<MIDIActivity>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE MIDI Activity" name="MIDI Activity" category="MIDI">
    <Audio>
        <Pin name="MIDI In" datatype="midi"/>
        <Pin name="Activity" datatype="bool" direction="out"/>
    </Audio>
</Plugin>
)XML");
}

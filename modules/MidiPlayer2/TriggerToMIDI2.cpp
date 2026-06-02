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
#include "mp_sdk_audio.h"
#include "mp_midi.h"

using namespace gmpi;

class TriggerToMIDI2 final : public MpBase2
{
	BoolInPin pinTrigger;
	BoolInPin pinGate;
	AudioInPin pinPitch;
	AudioInPin pinVelocity;
	IntInPin pinChannel;
	MidiOutPin pinMIDIOut;

	int midi_note = -1;

public:
	TriggerToMIDI2()
	{
		initializePin(pinTrigger);
		initializePin(pinGate);
		initializePin( pinPitch );
		initializePin( pinVelocity );
		initializePin( pinChannel );
		initializePin( pinMIDIOut );
	}

	void onSetPins() override
	{
		const auto note_on = pinGate.getValue() && (pinGate.isUpdated() || (pinTrigger.isUpdated() && pinTrigger.getValue()));
		const auto note_off = (pinGate.isUpdated() && !pinGate.getValue()) || (pinGate.getValue() && note_on);

		if (note_off)
		{
			const int chan = (std::max)(0, pinChannel.getValue()); // chan -1 -> chan 0

			const auto out = gmpi::midi_2_0::makeNoteOffMessage(
				(midi_note & 0x7f),
				0.5f,
				chan
			);

			pinMIDIOut.send(out.m);
		}

		if(note_on)
		{
			constexpr float MIDDLE_A = 69.f;
			const int chan = (std::max)(0, pinChannel.getValue()); // chan -1 -> chan 0

			float pitch_volts = pinPitch.getValue() * 10.f;
			const auto note_num = std::round( MIDDLE_A + (pitch_volts - 5.f) * 12.f);

			midi_note = std::clamp(static_cast<int>(note_num), 0, 127);

			const auto velocity = std::clamp(pinVelocity.getValue(), 1.0f / 127.0f, 1.0f);
			const auto out = gmpi::midi_2_0::makeNoteOnMessage(
				(midi_note & 0x7f),
				velocity,
				chan
			);

			pinMIDIOut.send(out.m);
		}
	}
};

namespace
{
	auto r = Register<TriggerToMIDI2>::withId(L"SE Trigger To MIDI2");
}

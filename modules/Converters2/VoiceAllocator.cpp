// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
#include <algorithm>
#include "Processor.h"

using namespace gmpi;

// Deals notes round-robin onto physical voices 1..Voices via the MIDI 2.0 note-on's Manufacturer Specific
// attribute. With Glide on, each voice glides from its own previous note (per-note glide start ahead of the note-on).
struct VoiceAllocator final : public Processor
{
	static constexpr int maxVoices = 127;
	static constexpr uint8_t glideStartController = 84; // Assignable Per-Note Controller, after MIDI 1.0 CC 84 Portamento Control.

	MidiInPin pinMIDIIn;
	MidiOutPin pinMIDIOut;
	IntInPin pinVoices;
	BoolInPin pinGlide;

	int previousVoice = 0;
	int lastKey[maxVoices + 1];

	midi::MidiConverter2 toMidi2;

	VoiceAllocator() :
		toMidi2([this](const midi2::message_view msg, int) { onMidi2Message(msg); })
	{
		std::fill(std::begin(lastKey), std::end(lastKey), -1);
	}

	void onMidiMessage(int /*pin*/, std::span<const uint8_t> midiMessage) override
	{
		toMidi2.processMidi(midiMessage, -1);
	}

	void onMidi2Message(midi2::message_view msg)
	{
		const auto header = midi2::decodeHeader(msg);
		if (header.messageType != midi2::ChannelVoice64 || header.status != midi2::NoteOn)
		{
			pinMIDIOut.send(msg);
			return;
		}

		const auto key = msg[2];
		const int voice = previousVoice = previousVoice % std::clamp(pinVoices.getValue(), 1, maxVoices) + 1;

		if (pinGlide.getValue() && lastKey[voice] >= 0)
		{
			auto glide = midi2::makeNotePitchMessage(key, static_cast<float>(lastKey[voice]), header.channel, header.group);
			glide.m[1] = static_cast<uint8_t>((midi2::PolyAssignableControlChange << 4) | header.channel);
			glide.m[3] = glideStartController;
			pinMIDIOut.send(midi2::message_view{ glide.m });
		}
		lastKey[voice] = key;

		// replaces any attribute the note-on already had (e.g. Pitch).
		uint8_t noteOn[8];
		std::copy(msg.begin(), msg.end(), noteOn);
		noteOn[3] = midi2::attribute_type::ManufacturerSpecific;
		noteOn[6] = 0; // voice, signed 16-bit.
		noteOn[7] = static_cast<uint8_t>(voice);
		pinMIDIOut.send(midi2::message_view{ noteOn });
	}
};

namespace
{
auto r = Register<VoiceAllocator>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Voice Allocator" name="Voice Allocator" category="SDK Examples">
    <Audio>
        <Pin name="MIDI In" datatype="midi"/>
        <Pin name="MIDI Out" datatype="midi" direction="out"/>
        <Pin name="Voices" datatype="int" default="4"/>
        <Pin name="Glide" datatype="bool"/>
    </Audio>
</Plugin>
)XML");
}

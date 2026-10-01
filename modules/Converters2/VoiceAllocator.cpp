// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
#include <algorithm>
#include <vector>
#include "Processor.h"

using namespace gmpi;

// Deals notes round-robin onto physical voices 1..Voices via the MIDI 2.0 note-on's Manufacturer Specific
// attribute. With Glide on, each voice glides from its own previous note (per-note glide start ahead of the note-on).
// Beyond Polyphony sounding notes (0 = no limit) the oldest note's voice is stolen (NRPN 120) before the new note starts.
struct VoiceAllocator final : public Processor
{
	static constexpr int maxVoices = 127;
	static constexpr uint8_t glideStartController = 84; // Assignable Per-Note Controller, after MIDI 1.0 CC 84 Portamento Control.
	static constexpr uint16_t stealVoiceNrpn = 120; // bank 0, after MIDI 1.0 CC 120 All Sound Off. Voice in the top 7 bits.

	MidiInPin pinMIDIIn;
	MidiOutPin pinMIDIOut;
	IntInPin pinVoices;
	BoolInPin pinGlide;
	IntInPin pinPolyphony;

	struct Note { int key; int voice; };

	int previousVoice = 0;
	int lastKey[maxVoices + 1];
	std::vector<Note> sounding; // oldest first.

	midi::MidiConverter2 toMidi2;

	VoiceAllocator() :
		toMidi2([this](const midi2::message_view msg, int) { onMidi2Message(msg); })
	{
		std::fill(std::begin(lastKey), std::end(lastKey), -1);
		sounding.reserve(maxVoices + 1);
	}

	void onMidiMessage(int /*pin*/, std::span<const uint8_t> midiMessage) override
	{
		toMidi2.processMidi(midiMessage, -1);
	}

	void onMidi2Message(midi2::message_view msg)
	{
		const auto header = midi2::decodeHeader(msg);
		const bool isNote = header.messageType == midi2::ChannelVoice64 && (header.status == midi2::NoteOn || header.status == midi2::NoteOff);
		if (!isNote || header.status == midi2::NoteOff)
		{
			if (isNote)
				forget([key = msg[2]](const Note& n) { return n.key == key; });

			pinMIDIOut.send(msg);
			return;
		}

		const auto key = msg[2];
		const int voice = previousVoice = previousVoice % std::clamp(pinVoices.getValue(), 1, maxVoices) + 1;

		// a note still sounding on the dealt voice is cut short by the engine.
		forget([voice](const Note& n) { return n.voice == voice; });

		for (const int polyphony = pinPolyphony.getValue(); polyphony > 0 && static_cast<int>(sounding.size()) >= polyphony;)
		{
			const auto steal = midi2::makeNrpnRaw(stealVoiceNrpn, static_cast<uint32_t>(sounding.front().voice) << 25, header.channel, header.group);
			pinMIDIOut.send(midi2::message_view{ steal.m });
			sounding.erase(sounding.begin());
		}
		sounding.push_back({ key, voice });

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

	template<typename Pred>
	void forget(Pred pred)
	{
		if (auto it = std::find_if(sounding.begin(), sounding.end(), pred); it != sounding.end())
			sounding.erase(it);
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
        <Pin name="Polyphony" datatype="int"/>
    </Audio>
</Plugin>
)XML");
}

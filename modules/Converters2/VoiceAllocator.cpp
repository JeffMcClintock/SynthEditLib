// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
#include <algorithm>
#include <vector>
#include "Processor.h"

using namespace gmpi;

// Voice Allocator: an example of choosing SynthEdit's physical voices from MIDI.
//
// Normally a poly container's own allocator decides which physical voice plays each note.
// A module upstream of 'MIDI to CV 2' can decide instead, using three MIDI 2.0 messages:
//
//   Pick a voice   Note On with attribute type 1 (Manufacturer Specific), attribute data = voice, signed 16-bit (-1 = SynthEdit chooses).
//   Glide start    Assignable Per-Note Controller 84 on the same key, sent BEFORE the note-on: the pitch the note glides from.
//   Steal a voice  NRPN bank 0, index 120, voice in the top 7 bits of the value: that voice fades out over 20ms.
//
// Physical voices are numbered 1 to (polyphony + reserve voices). Voice 0 belongs to monophonic modules.
// Out-of-range voices are ignored, as is every request while the container is in a mono mode.
// The container's polyphony limit still applies, so exceeding it steals a voice as usual.
//
// This example deals notes round-robin onto voices 1..Voices. With Glide on, each voice glides from its own
// previous note. Beyond Polyphony sounding notes (0 = no limit) the oldest note's voice is stolen first.
struct VoiceAllocator final : public Processor
{
	static constexpr int maxVoices = 127; // the steal message carries 7 bits of voice number.
	static constexpr uint8_t glideStartController = 84; // after MIDI 1.0 CC 84 Portamento Control.
	static constexpr uint16_t stealVoiceNrpn = 120; // after MIDI 1.0 CC 120 All Sound Off.

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

	// MIDI 1.0 can't carry note attributes or per-note controllers, so work in MIDI 2.0 (MIDI 2.0 input passes straight through).
	void onMidiMessage(int /*pin*/, std::span<const uint8_t> midiMessage) override
	{
		toMidi2.processMidi(midiMessage, -1);
	}

	void onMidi2Message(midi2::message_view msg)
	{
		const auto header = midi2::decodeHeader(msg);
		const bool isNote = header.messageType == midi2::ChannelVoice64 && (header.status == midi2::NoteOn || header.status == midi2::NoteOff);

		// Note-offs, and everything else, go out unchanged. SynthEdit finds a note's voice by its key.
		if (!isNote || header.status == midi2::NoteOff)
		{
			if (isNote)
				forget([key = msg[2]](const Note& n) { return n.key == key; });

			pinMIDIOut.send(msg);
			return;
		}

		const auto key = msg[2];
		const int voice = previousVoice = previousVoice % std::clamp(pinVoices.getValue(), 1, maxVoices) + 1;

		// Requesting a voice that is still sounding is fine: SynthEdit fades it over 5ms and starts the note once it is silent.
		forget([voice](const Note& n) { return n.voice == voice; });

		// Steal a voice: NRPN (Assignable Controller, status 0x3), bank 0, index 120, the voice in the top 7 bits of the 32-bit value.
		// From MIDI 1.0 it's CC 99 = 0, CC 98 = 120, CC 6 = voice. It addresses a voice, not a key, so the stolen note's later note-off is harmless.
		for (const int polyphony = pinPolyphony.getValue(); polyphony > 0 && static_cast<int>(sounding.size()) >= polyphony;)
		{
			const auto steal = midi2::makeNrpnRaw(stealVoiceNrpn, static_cast<uint32_t>(sounding.front().voice) << 25, header.channel, header.group);
			pinMIDIOut.send(midi2::message_view{ steal.m });
			sounding.erase(sounding.begin());
		}
		sounding.push_back({ key, voice });

		// Glide start: Assignable Per-Note Controller (status 0x1) 84 on this key, the value an absolute pitch in semitones.
		// It must arrive BEFORE the note-on, which uses it once. It applies when the voice starts fresh, over MIDI to CV 2's portamento time.
		// makeNotePitchMessage builds Registered Per-Note Controller 3 (absolute pitch) in the right format; re-address it to 84.
		if (pinGlide.getValue() && lastKey[voice] >= 0)
		{
			auto glide = midi2::makeNotePitchMessage(key, static_cast<float>(lastKey[voice]), header.channel, header.group);
			glide.m[1] = static_cast<uint8_t>((midi2::PolyAssignableControlChange << 4) | header.channel);
			glide.m[3] = glideStartController;
			pinMIDIOut.send(midi2::message_view{ glide.m });
		}
		lastKey[voice] = key;

		// Pick the voice: attribute type 1 (Manufacturer Specific) in byte 3, the voice as a signed 16-bit value in bytes 6-7.
		// This replaces any attribute the note-on already had (e.g. Pitch).
		uint8_t noteOn[8];
		std::copy(msg.begin(), msg.end(), noteOn);
		noteOn[3] = midi2::attribute_type::ManufacturerSpecific;
		noteOn[6] = 0;
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

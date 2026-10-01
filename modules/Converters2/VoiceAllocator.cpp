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
//                  Send the note's own pitch to stop it gliding. With no glide start, SynthEdit glides overlapping notes from the most recent one.
//   Steal a voice  NRPN bank 0, index 120, voice in the top 7 bits of the value: that voice fades out over 20ms.
//
// Physical voices are numbered 1 to (polyphony + reserve voices). Voice 0 belongs to monophonic modules.
// Out-of-range voices are ignored, as is every request while the container is in a mono mode.
// The container's polyphony limit still applies, so exceeding it steals a voice as usual.
//
// Strategies:
//   Round Robin             Each note takes the next free voice of 1..Voices. Glide is left to SynthEdit.
//   Round Robin with Glide  As Round Robin, and every note glides from the last note played, whichever voice played it.
//   Mono                    Every note plays on voice 1 and glides from the previous note. Releasing the
//                           sounding key returns to the most recent key still held.
//   Highest Note Glides     As Round Robin, but only a note above every held key glides (from the previous top note).
// Beyond Polyphony sounding notes (0 = no limit) the oldest note's voice is stolen first.
struct VoiceAllocator final : public Processor
{
	enum Strategy { RoundRobin, RoundRobinGlide, Mono, HighestNoteGlides };

	static constexpr int maxVoices = 127; // the steal message carries 7 bits of voice number.
	static constexpr uint8_t glideStartController = 84; // after MIDI 1.0 CC 84 Portamento Control.
	static constexpr uint16_t stealVoiceNrpn = 120; // after MIDI 1.0 CC 120 All Sound Off.

	MidiInPin pinMIDIIn;
	MidiOutPin pinMIDIOut;
	IntInPin pinVoices;
	IntInPin pinStrategy;
	IntInPin pinPolyphony;

	struct Note { int key; int voice; };
	struct HeldKey { uint8_t key; uint8_t noteOn[8]; };

	int previousVoice = 0;
	int lastNote = -1;          // the last note played, on any voice.
	int topKey = -1;            // Highest Note Glides: the last note that was the highest when it started.
	std::vector<Note> sounding; // oldest first.
	std::vector<HeldKey> held;  // in the order pressed.

	midi::MidiConverter2 toMidi2;

	VoiceAllocator() :
		toMidi2([this](const midi2::message_view msg, int) { onMidi2Message(msg); })
	{
		sounding.reserve(maxVoices + 1);
		held.reserve(128);
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

		// Everything but notes goes out unchanged.
		if (!isNote)
		{
			pinMIDIOut.send(msg);
			return;
		}

		const auto key = msg[2];

		// Note-offs go out unchanged too. SynthEdit finds a note's voice by its key.
		if (header.status == midi2::NoteOff)
		{
			forget(sounding, [key](const Note& n) { return n.key == key; });
			forget(held, [key](const HeldKey& h) { return h.key == key; });

			// Mono: releasing the sounding key (the last note played) plays the most recent key still held, gliding back to it.
			if (pinStrategy.getValue() == Mono && key == lastNote && !held.empty())
				play(held.back().noteOn, header, false);

			pinMIDIOut.send(msg);
			return;
		}

		const bool isHighest = std::all_of(held.begin(), held.end(), [key](const HeldKey& h) { return h.key < key; });

		HeldKey h{ key };
		std::copy(msg.begin(), msg.end(), h.noteOn);
		held.push_back(h);

		play(msg, header, isHighest);
	}

	// Sends one note-on to a voice, preceded by any steal and glide start it needs.
	void play(midi2::message_view msg, const midi2::headerInfo& header, bool isHighest)
	{
		const auto strategy = pinStrategy.getValue();
		const auto key = msg[2];
		const int voice = strategy == Mono ? 1 : nextVoice();

		// Requesting a voice that is still sounding is fine: SynthEdit fades it over 5ms and starts the note once it is silent.
		// So in Mono each note restarts voice 1 rather than continuing it legato.
		forget(sounding, [voice](const Note& n) { return n.voice == voice; });

		// Steal a voice: NRPN (Assignable Controller, status 0x3), bank 0, index 120, the voice in the top 7 bits of the 32-bit value.
		// From MIDI 1.0 it's CC 99 = 0, CC 98 = 120, CC 6 = voice. It addresses a voice, not a key, so the stolen note's later note-off is harmless.
		for (const int polyphony = pinPolyphony.getValue(); polyphony > 0 && static_cast<int>(sounding.size()) >= polyphony;)
		{
			const auto steal = midi2::makeNrpnRaw(stealVoiceNrpn, static_cast<uint32_t>(sounding.front().voice) << 25, header.channel, header.group);
			pinMIDIOut.send(midi2::message_view{ steal.m });
			sounding.erase(sounding.begin());
		}
		sounding.push_back({ key, voice });

		// Which key this note glides from. Its own key means no glide; -1 sends nothing and leaves glide to SynthEdit.
		int glideFrom = key;
		switch (strategy)
		{
		case RoundRobin:
			glideFrom = -1;
			break;

		case RoundRobinGlide:
		case Mono:
			if (lastNote >= 0)
				glideFrom = lastNote;
			break;

		case HighestNoteGlides:
			if (isHighest)
			{
				if (topKey >= 0)
					glideFrom = topKey;
				topKey = key;
			}
			break;
		}
		lastNote = key;

		// Glide start: Assignable Per-Note Controller (status 0x1) 84 on this key, the value an absolute pitch in semitones.
		// It must arrive BEFORE the note-on, which uses it once. It applies when the voice starts fresh, over MIDI to CV 2's portamento time.
		// makeNotePitchMessage builds Registered Per-Note Controller 3 (absolute pitch) in the right format; re-address it to 84.
		if (glideFrom >= 0)
		{
			auto glide = midi2::makeNotePitchMessage(key, static_cast<float>(glideFrom), header.channel, header.group);
			glide.m[1] = static_cast<uint8_t>((midi2::PolyAssignableControlChange << 4) | header.channel);
			glide.m[3] = glideStartController;
			pinMIDIOut.send(midi2::message_view{ glide.m });
		}

		// Pick the voice: attribute type 1 (Manufacturer Specific) in byte 3, the voice as a signed 16-bit value in bytes 6-7.
		// This replaces any attribute the note-on already had (e.g. Pitch).
		uint8_t noteOn[8];
		std::copy(msg.begin(), msg.end(), noteOn);
		noteOn[3] = midi2::attribute_type::ManufacturerSpecific;
		noteOn[6] = 0;
		noteOn[7] = static_cast<uint8_t>(voice);
		pinMIDIOut.send(midi2::message_view{ noteOn });
	}

	// The next voice of 1..Voices in turn that isn't sounding. If all are, the next in turn, cutting its note short.
	int nextVoice()
	{
		const int voices = std::clamp(pinVoices.getValue(), 1, maxVoices);
		for (int i = 0; i < voices; ++i)
		{
			const int voice = (previousVoice + i) % voices + 1;
			if (std::none_of(sounding.begin(), sounding.end(), [voice](const Note& n) { return n.voice == voice; }))
				return previousVoice = voice;
		}
		return previousVoice = previousVoice % voices + 1;
	}

	template<typename T, typename Pred>
	static void forget(std::vector<T>& list, Pred pred)
	{
		if (auto it = std::find_if(list.begin(), list.end(), pred); it != list.end())
			list.erase(it);
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
        <Pin name="Strategy" datatype="enum" metadata="Round Robin,Round Robin with Glide,Mono,Highest Note Glides"/>
        <Pin name="Polyphony" datatype="int" default="4"/>
    </Audio>
</Plugin>
)XML");
}

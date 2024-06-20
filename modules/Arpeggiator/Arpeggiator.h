#ifndef ARPEGGIATOR_H_INCLUDED
#define ARPEGGIATOR_H_INCLUDED

#include <vector>
#include "mp_sdk_audio.h"
#include "mp_midi.h"

class Arpeggiator : public MpBase
{
public:
	Arpeggiator( IMpUnknown* host );
	void onMidiMessage( int pin, unsigned char* midiMessage, int size ) override;
	void onMidi2Message(const gmpi::midi::message_view& msg);
	void onSetPins(void) override;
	void step();
	void PlayingNoteOff();
	void setOutputHoldPedal(bool newHoldPedal);

private:
	BoolInPin pinClock;
	BoolInPin pinReset;
	IntInPin pinMode;
	IntInPin pinOctave;
	MidiInPin pinMIDIIn;
	MidiOutPin pinMIDIOut;

	float pulseOutVal;
	std::vector<bool> keyStates;
	std::vector<float> keyVelocities;
	std::vector<bool> keyHeld;
	int currentKeyNumber_;
	int currentOctave_;
	int playingKey_;
	bool directionUp_;
	bool inputHoldPedal_;
	bool outputHoldPedal_;
	gmpi::midi_2_0::MidiConverter2 midiConverter;
};

#endif


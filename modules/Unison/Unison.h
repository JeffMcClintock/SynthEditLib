#ifndef UNISON_H_INCLUDED
#define UNISON_H_INCLUDED

#include <vector>
#include <algorithm>
#include <math.h>
#include "../se_sdk3/mp_sdk_audio.h"
#include "../se_sdk3/mp_midi.h"

#define VoltageToTimecent(v) ( ((v) * 12000.f) / 10.f - 8000.f )
#define TimecentToSecond(t) powf( 2.0f, (t) / 1200.0f )

class GlideManager
{
public:
	GlideManager() :
		portamento_target(-1000.0f)
		, portamento_increment(0.0f)
		, portamento_pitch(-1000.0f)
	{}

	bool isActive()
	{
		return portamento_increment != 0.0f;
	}

	inline float SemitoneToOctave(float pitchInSemitones)
	{
		const int lMIDDLE_A = 69;
		const int VOLT_RANGE = 10;
	    const float lFLOAT_STEP_PER_NOTE = (1.f / (VOLT_RANGE * 12.f));
		return 0.5f + (pitchInSemitones - lMIDDLE_A) * lFLOAT_STEP_PER_NOTE;
	}
	inline float OctaveToSemitone(float pitchInOctaves)
	{
		const int lMIDDLE_A = 69;
		const int VOLT_RANGE = 10;
		return (pitchInOctaves - 0.5f) * 120.0f + lMIDDLE_A;
	}

	void JumpToSemitone(float pitchInSemitones)
	{
		JumpToOctave(SemitoneToOctave(pitchInSemitones));
	}

	void JumpToOctave(float pitchInOctaves)
	{
		portamento_pitch = portamento_target = pitchInOctaves;
		portamento_increment = 0.0f;
//		fprintf(stderr, "JumpToOctave: time: %f s target: %f Oct, inc %f\n", time, portamento_target, portamento_increment );
	}

	inline void GlideToSemitone(float pitchInSemitones, float timePin, int mode, float sampleRate)
	{
		GlideToOctave(SemitoneToOctave(pitchInSemitones), timePin, mode, sampleRate);
	}

	void GlideToOctave(float pitchInOctaves, float timePin, int mode, float sampleRate)
	{
		if (portamento_pitch == -1000.0f) // very first note? Jump.
		{
			JumpToOctave(pitchInOctaves);
			return;
		}
		portamento_target = pitchInOctaves;

		// glide pitch toward new pitch
		// calculate time in samples for glide
		// same scale as an envelope seqment - 2 Volts for quicker response near zero (0.0025 seconds)
		float time = TimecentToSecond(VoltageToTimecent(timePin * 10.f - 2.f));

		// Prevent floating point overflow exception due to time being VERY small.
		if (time < 0.00001f) // approx 1 sample at 96k
		{
//			time = 0.0f;
            JumpToOctave(pitchInOctaves);
		}
        
		time = time * sampleRate;

		if (mode == 0) // Constant time?
		{
			portamento_increment = (portamento_target - portamento_pitch) / time;  //difference v
		}
		else // Constant Rate.
		{
			// Distance fixed at 1 octave.
			if (portamento_target > portamento_pitch)
			{
				portamento_increment = 0.1f / time;
			}
			else
			{
				portamento_increment = -0.1f / time;
			}
		}
        
//		fprintf(stderr, "GlideToOctave: time: %f s target: %f Oct, inc %f\n", time, portamento_target, portamento_increment );
	}
	bool process(int sampleFrames)
	{
		assert(portamento_increment != 0.f || portamento_pitch == portamento_target);

		if (portamento_increment == 0.0f)
		{
        	return true; // can sleep.
        }
        
        if( portamento_increment > 0.0f )
        {
        	while( sampleFrames > 0 && portamento_pitch < portamento_target )
            {
				portamento_pitch += portamento_increment;
                --sampleFrames;
            }
        }
        else
        {
        	while( sampleFrames > 0 && portamento_pitch > portamento_target )
            {
				portamento_pitch += portamento_increment;
                --sampleFrames;
            }
        }

		bool done = sampleFrames > 0;
        if( done ) // done?
        {
			portamento_pitch = portamento_target;
			portamento_increment = 0.0f;
        }
        
//		fprintf(stderr, "Glider::Process: portamento_pitch: %f target %f inc %f can-sleep %d\n", portamento_pitch, portamento_target, portamento_increment, (int)done);
		return done;
	}

	float GetPitchSemitones()
	{
		return OctaveToSemitone(portamento_pitch);
	}

private:
	float portamento_target;
	float portamento_increment;
	float portamento_pitch;
};

class Unison : public MpBase, public GlideManager
{
	static const int midiVoiceCount = 128;
	static const int MCV_NOTE_MEM_SIZE = 8; // power-of-2 please.
	char note_memory[MCV_NOTE_MEM_SIZE];
	bool note_status[midiVoiceCount];
	float noteVelocity[midiVoiceCount];
	std::vector<int> keyVoiceAssign;
	int note_memory_idx;
	unsigned char ccGlide[midiVoiceCount];
	unsigned char ccPan[midiVoiceCount];
	bool notesHeld;
	gmpi::midi_2_0::MidiConverter2 midiConverter;

public:
	Unison( IMpUnknown* host );
	void subProcess( int bufferOffset, int sampleFrames );
	virtual void onSetPins(void);
	void onMidiMessage( int pin, unsigned char* midiMessage, int size ) override
	{
		// convert everything to MIDI 2.0
		midiConverter.processMidi({ (const uint8_t*)midiMessage, size }, -1);
	}
	void onMidi2Message(const gmpi::midi::message_view& msg);
	void TuneKey(int keyNumber, float tune, bool glide = false, bool retrigger = false, int timeDelay = 0);
	void MonoGlide(int MainNote);

	void NoteOn(unsigned char incommingKey, unsigned char outgoingKey, float velocity)
	{
		assert(keyVoiceAssign[outgoingKey] == -1);
		assert(note_status[incommingKey] == true);
		assert(velocity > 0.0f);
		assert(outgoingKey < 128);

//		unsigned char midiMessage[] = { GmpiMidi::MIDI_NoteOn, outgoingKey, velocity };

		const auto out = gmpi::midi_2_0::makeNoteOnMessage(
			outgoingKey,
			velocity
		);
		pinMIDIOut.send((const unsigned char*)&out, sizeof(out));

		keyVoiceAssign[outgoingKey] = incommingKey;
	}

	void NoteOff(unsigned char incommingKey)
	{
		assert(note_status[incommingKey] == false);

//		unsigned char midiMessage[] = { GmpiMidi::MIDI_NoteOff, 0, 64 };
		for (int n = 0; n < 128; ++n)
		{
			if (keyVoiceAssign[n] == incommingKey)
			{
//				midiMessage[1] = n;
				const auto out = gmpi::midi_2_0::makeNoteOffMessage(
					n,
					0.5f
				);
				pinMIDIOut.send((const unsigned char*)&out, sizeof(out));
				keyVoiceAssign[n] = -1;
			}
		}
	}
	void AllNotesOff()
	{
//		unsigned char midiMessage[] = { GmpiMidi::MIDI_NoteOff, 0, 64 };
		for (int n = 0; n < 128; ++n)
		{
			if (keyVoiceAssign[n] != -1 )
			{
//				midiMessage[1] = n;

				const auto out = gmpi::midi_2_0::makeNoteOffMessage(
					n,
					0.5f
				);
				pinMIDIOut.send((const unsigned char*)&out, sizeof(out));

				keyVoiceAssign[n] = -1;
			}
		}
	}

	inline int getVoiceCount()
	{
		return ( std::max )( 1, pinVoices.getValue() ); // cope with invalid presets having voices=0
	}

private:
	AudioInPin pinSpread;
	IntInPin pinVoices;
	MidiInPin pinMIDIIn;
	MidiOutPin pinMIDIOut;
	IntInPin pinMode;
	IntInPin pinRetrigger;
	AudioInPin pinPortamentoTime;
	IntInPin pinPortamentoMode;
	IntInPin pinAutoGlide;

	float outTuning[128];
};

#endif


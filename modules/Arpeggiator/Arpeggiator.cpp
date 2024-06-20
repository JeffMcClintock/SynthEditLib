#include "./Arpeggiator.h"
#include <stdlib.h> // rand() on linux.
#include "../se_sdk3/mp_midi.h"

REGISTER_PLUGIN ( Arpeggiator, L"SE Arpeggiator" );

using namespace gmpi;

enum ArpModes{ AM_OFF, AM_UP, AM_DOWN, AM_UPDOWN, AM_RANDOM };

Arpeggiator::Arpeggiator( IMpUnknown* host ) : MpBase( host )
	, pulseOutVal(0.0f)
	, currentKeyNumber_(0)
	, currentOctave_(0)
	, playingKey_(-1)
	, directionUp_(true)
	, inputHoldPedal_(false)
	, outputHoldPedal_(false)
	// provide a lambda to accept converted MIDI 2.0 messages
	, midiConverter(
		[this](const midi::message_view& msg, int offset)
		{
			onMidi2Message(msg);
		}
	)
{
	// Register pins.
	initializePin( pinClock );
	initializePin( pinReset);
	initializePin( pinMode );
	initializePin( pinOctave );
	initializePin( pinMIDIIn );
	initializePin( pinMIDIOut);
	
	keyStates.assign( 128, false );
	keyHeld.assign( 128, false );
	keyVelocities.assign(128, 64);
}

// passes all MIDI to the converter.
void Arpeggiator::onMidiMessage(int pin, unsigned char* midiMessage, int size)
{
	midi::message_view msg((const uint8_t*)midiMessage, size);

	// convert everything to MIDI 2.0
	midiConverter.processMidi(msg, -1);
}

void Arpeggiator::onMidi2Message(const gmpi::midi::message_view& msg)
{
	const auto header = gmpi::midi_2_0::decodeHeader(msg);

	// only 8-byte messages supported.
	if (header.messageType != gmpi::midi_2_0::ChannelVoice64)
		return;

	bool sendThrough = true;

	switch (header.status)
	{
		case gmpi::midi_2_0::NoteOn:
		{
			const auto note = gmpi::midi_2_0::decodeNote(msg);
			const auto b2 = note.noteNumber;

			// Playing a clean note on (no notes already held?).
			bool reset = true;
			for( int k = 0 ; k < 128 ; ++k )
			{
				if( keyHeld[k] )
				{
					reset = false;
					break;
				}
			}

			// If so start sequence at bottom key.
			if( reset )
			{
				if( pinMode == AM_DOWN )
				{
					currentKeyNumber_ = 127;
					currentOctave_ = pinOctave;
				}
				else
				{
					currentKeyNumber_ = 0;
					currentOctave_ = 0;
				}
			}

			if( inputHoldPedal_ )
			{
				if( reset ) // clear prev held notes.
				{
					for( int k = 0 ; k < 128 ; ++k )
					{
						keyHeld[k] = keyStates[k] = false;
					}
				}
			}

			keyStates[b2] = true;
			keyVelocities[b2] = note.velocity;
			keyHeld[b2] = true;
			sendThrough = false;

		    if( reset && pinClock.getValue()) // play posibly late first note.
		    {
				// Turn off current note if any.
		        if( playingKey_ >= 0 )
		        {
			        //midiMessage[0] = GmpiMidi::MIDI_NoteOff; // Force channel zero.
			        //midiMessage[1] = playingKey_;
			        //midiMessage[2] = 0x60;
			        //pinMIDIOut.send( midiMessage, sizeof(midiMessage) );

					const auto out = gmpi::midi_2_0::makeNoteOffMessage(
						playingKey_,
						0.5f
					);

					pinMIDIOut.send(out.m);

			        playingKey_ = -1;
		        }
			    step();
		    }
		}
		break;

		case gmpi::midi_2_0::NoteOff:
		{
			const auto note = gmpi::midi_2_0::decodeNote(msg);
			const auto b2 = note.noteNumber;

			sendThrough = false;
			if( !inputHoldPedal_ || pinMode == AM_OFF )
			{
				keyStates[b2] = false;
			}
			keyHeld[b2] = false;
		}
		break;

		case gmpi::midi_2_0::ControlChange:
		{
			const auto controller = gmpi::midi_2_0::decodeController(msg);
			const auto b2 = controller.type;

			if (b2 == 123) // MIDI 'All Notes Off' CC.
			{
				// Turn off current note if any.
				if (playingKey_ >= 0)
				{
					//midiMessage[0] = GmpiMidi::MIDI_NoteOff; // Force channel zero.
					//midiMessage[1] = playingKey_;
					//midiMessage[2] = 0x60;

					const auto out = gmpi::midi_2_0::makeNoteOffMessage(
						playingKey_,
						0.5f
					);

					pinMIDIOut.send(out.m);

					playingKey_ = -1;
				}

				for (int n = 0; n < 128; ++n)
				{
					keyStates[n] = false;
					keyHeld[n] = false;
				}
			}

			if (b2 == 64) // MIDI 'Hold' CC.
			{
				sendThrough = false;

				bool prevholdPedal = inputHoldPedal_;
				inputHoldPedal_ = controller.value > 0.5f;// b3 >= 64;

				if (pinMode == AM_OFF)
				{
					setOutputHoldPedal(inputHoldPedal_);
				}
				else
				{
					if( prevholdPedal && !inputHoldPedal_ )
					{
						PlayingNoteOff();
						for( int k = 0 ; k < 128 ; ++k )
						{
							if( keyStates[k] && !keyHeld[k] )
							{
								keyStates[k] = false;
							}
						}
					}
				}
			}
		}
		break;

	default:
		break;
	}

	if( sendThrough || pinMode == AM_OFF )
	{
		pinMIDIOut.send(msg.begin(), msg.size());
	}
}

void Arpeggiator::step()
{
	if( !pinClock.getValue() )
	{
		pulseOutVal = 0.0f;

		PlayingNoteOff();
	}
	else
	{
		pulseOutVal = 1.0f;

		int newkeyNumber = currentKeyNumber_;

		// when hitting chords in hold mode, need sequence to restart from lowest note.
		// Otherwise it chooses note 'next' from previous chord's note.

		switch( pinMode )
		{
		case AM_UP:
			do
			{
				newkeyNumber = (newkeyNumber + 1) & 0x7f;
			}while( keyStates[newkeyNumber] == false && newkeyNumber != currentKeyNumber_ );

			if( newkeyNumber <= currentKeyNumber_ ) // next octave?
			{
				if( currentOctave_ >= pinOctave )
				{
					currentOctave_ = 0;
				}
				else
				{
					++currentOctave_;
				};
			}
			break;

		case AM_DOWN:
			do
			{
				newkeyNumber = (newkeyNumber - 1) & 0x7f;
			}while( keyStates[newkeyNumber] == false && newkeyNumber != currentKeyNumber_ );

			if( newkeyNumber >= currentKeyNumber_ ) // next octave?
			{
				if( currentOctave_ == 0 )
				{
					currentOctave_ = pinOctave;
				}
				else
				{
					--currentOctave_;
				};
			}
			break;

		case AM_UPDOWN:
			{
				int retry = 2;
				while( retry > 0 )
				{
					if( directionUp_ )
					{
						do
						{
							newkeyNumber = (newkeyNumber + 1) & 0x7f;
						}while( keyStates[newkeyNumber] == false && newkeyNumber != currentKeyNumber_ );

						if( newkeyNumber <= currentKeyNumber_ ) // wrapped, next octave?
						{
							if( currentOctave_ >= pinOctave ) // change direction?
							{
								newkeyNumber = currentKeyNumber_;
								directionUp_ = false;
								--retry;
							}
							else
							{
								++currentOctave_;
								break;
							};
						}
						else
						{
							break;
						}
					}
					else
					{
						do
						{
							newkeyNumber = (newkeyNumber - 1) & 0x7f;
						}while( keyStates[newkeyNumber] == false && newkeyNumber != currentKeyNumber_ );

						if( newkeyNumber >= currentKeyNumber_ ) // next octave?
						{
							if( currentOctave_ == 0 )
							{
								newkeyNumber = currentKeyNumber_;
								directionUp_ = true;
								--retry;
							}
							else
							{
								--currentOctave_;
								break;
							};
						}
						else
						{
							break;
						}
					}
				}
			}

			break;

		case AM_RANDOM:
			int steps = 1 + (rand() & 0x07);
			for( int i = steps ; i > 0 ; --i )
			{
				do
				{
					newkeyNumber = (newkeyNumber - 1) & 0x7f;
				}while( keyStates[newkeyNumber] == false && newkeyNumber != currentKeyNumber_ );
				currentKeyNumber_ = newkeyNumber;
			}
			currentOctave_ = (currentOctave_ + steps) % (1+pinOctave);
			break;
		}

		if( keyStates[newkeyNumber] )
		{
			currentKeyNumber_ = newkeyNumber;
			playingKey_ = currentKeyNumber_ + currentOctave_ * 12;

			// Prevent illeagally high notes.
			while( playingKey_ > 127 )
			{
				playingKey_ -= 12;
			}

			//midiMessage[0] = GmpiMidi::MIDI_NoteOn; // Force channel zero.
			//midiMessage[1] = playingKey_;
			//midiMessage[2] = keyVelocities[playingKey_];

			const auto out = gmpi::midi_2_0::makeNoteOnMessage(
				playingKey_,
				keyVelocities[playingKey_]
			);

			pinMIDIOut.send(out.m);
		}
		else
		{
			// No keys held. Reset so next chord starts arp at lowest note.
			if( pinMode == AM_DOWN )
			{
				currentKeyNumber_ = 127;
				currentOctave_ = pinOctave;
			}
			else
			{
				currentKeyNumber_ = 0;
				currentOctave_ = 0;
			}
		}
	}
}

void Arpeggiator::PlayingNoteOff()
{
	if( playingKey_ >= 0 )
	{
		//unsigned char midiMessage[] = { GmpiMidi::MIDI_NoteOff, 0x00, 0x60 };
		//midiMessage[1] = playingKey_;

		const auto out = gmpi::midi_2_0::makeNoteOffMessage(
			playingKey_,
			0.5f
		);

		pinMIDIOut.send(out.m);

		playingKey_ = -1;
	}
}

void Arpeggiator::onSetPins(void)
{
	// Set processing method.
	if (pinMode.isUpdated())
	{
		if (pinMode == AM_OFF)
		{
			PlayingNoteOff();

			// Pass-through hold-pedal.
			setOutputHoldPedal(inputHoldPedal_);
		}
		else
		{
			setOutputHoldPedal(false);
		}
	}

	if (pinReset.isUpdated() && pinReset.getValue())
	{
		if (pinMode == AM_DOWN)
		{
			currentKeyNumber_ = 127;
			currentOctave_ = pinOctave;
		}
		else
		{
			currentKeyNumber_ = 0;
			currentOctave_ = 0;
		}

		if (pinClock.getValue() && !pinClock.isUpdated()) // play posibly late first note. Unless we're about to step anyway.
		{
			// Turn off current note if any.
			if (playingKey_ >= 0)
			{
				//unsigned char midiMessage[3]{ GmpiMidi::MIDI_NoteOff, static_cast<unsigned char>(playingKey_), 0x60 };
				//pinMIDIOut.send(midiMessage, sizeof(midiMessage));

				const auto out = gmpi::midi_2_0::makeNoteOffMessage(
					playingKey_,
					0.5f
				);

				pinMIDIOut.send(out.m);

				playingKey_ = -1;
			}
			step();
		}
	}

	if (pinClock.isUpdated() && pinMode != AM_OFF)
	{
		step();
	}
}

void Arpeggiator::setOutputHoldPedal(bool newOutputHoldPedal)
{
	if (newOutputHoldPedal != outputHoldPedal_)
	{
		outputHoldPedal_ = newOutputHoldPedal;

		//midiMessage[0] = GmpiMidi::MIDI_ControlChange; // Force channel zero.
		//midiMessage[1] = 64; // hold pedal.
		//midiMessage[2] = outputHoldPedal_ ? 0x60 : 0;

		const auto out = gmpi::midi_2_0::makeController(
			64, // hold pedal.
			outputHoldPedal_ ? 0.8f : 0.0f
		);
		pinMIDIOut.send(out.m);
	}
}

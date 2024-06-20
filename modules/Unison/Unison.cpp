#include "./Unison.h"

using namespace gmpi;

REGISTER_PLUGIN ( Unison, L"SEUnison" );

Unison::Unison( IMpUnknown* host ) : MpBase( host )
, note_memory_idx(0)
, notesHeld(false)
, midiConverter(
	// provide a lambda to accept converted MIDI 2.0 messages
	[this](const midi::message_view& msg, int offset)
		{
			onMidi2Message(msg);
		}
	)
{
	// Register pins.
	initializePin( 0, pinSpread );
	initializePin( 1, pinVoices );
	initializePin( 2, pinMIDIIn );
	initializePin( 3, pinMIDIOut );
	initializePin(4, pinMode);
	initializePin(5, pinRetrigger);
	initializePin(6, pinPortamentoTime);
	initializePin(7, pinPortamentoMode);
	initializePin(8, pinAutoGlide);
	
	keyVoiceAssign.assign( 128, -1 );
	memset(note_status, 0, sizeof(note_status));
	for (int i = 0; i < MCV_NOTE_MEM_SIZE; ++i)
	{
		note_memory[i] = -1;
	}
	for (int i = 0; i < 128; ++i)
	{
		ccGlide[i] = -1;
		ccPan[i] = -1;
		outTuning[i] = -1;
	}
}

void Unison::onMidi2Message(const midi::message_view& msg)
{
	bool polyGlide = true;
	bool sendThrough = true;

	const auto header = gmpi::midi_2_0::decodeHeader(msg);

	// only 8-byte messages supported.
	if (header.messageType != gmpi::midi_2_0::ChannelVoice64)
		return;

	switch (header.status)
	{
		case gmpi::midi_2_0::NoteOn:
		{
			sendThrough = false;

			const auto note = gmpi::midi_2_0::decodeNote(msg);
			int MainNote = note.noteNumber;
			note_status[MainNote] = true;
			noteVelocity[MainNote] = note.velocity;

			bool glide = notesHeld;

			notesHeld = true;

			note_memory_idx++;
			note_memory_idx %= MCV_NOTE_MEM_SIZE;
			note_memory[note_memory_idx] = MainNote;

			if (pinMode == 1 && glide) // Mono and note-held.
			{
				MonoGlide(MainNote);
			}
			else
			{
				if (pinAutoGlide == 0 || glide )
				{
					GlideManager::GlideToSemitone((float)MainNote, pinPortamentoTime.getValue(), pinPortamentoMode, getSampleRate());
					setSleep(false); // stay awake.
				}
				else
				{
					GlideManager::JumpToSemitone((float)MainNote);
				}

				for( int v = 0; v < getVoiceCount(); ++v )
				{
					int noteNumber = -1;

					if (pinMode == 1) // Mono.
					{
						noteNumber = v;
					}
					else
					{
						if (noteNumber == -1)
						{
							// Try to assign same outgoing key to any incomming key, although due
							// to overlap it's not always possible.
							int roundRobinKeyNumber = MainNote * getVoiceCount();

							for (int n = 0; n < 128; ++n)
							{
								roundRobinKeyNumber = (roundRobinKeyNumber + 1) & 127;
								if (keyVoiceAssign[roundRobinKeyNumber] == -1)
								{
									noteNumber = roundRobinKeyNumber;
									break;
								}
							}
						}
					}

					if (noteNumber >= 0)
					{
						float detune = 0.0f;
						float pan = 64.f;
						if (getVoiceCount() > 1)
						{
							detune = pinSpread * ((float)v / (float)(getVoiceCount() - 1) - 0.5f);
							pan = 127.0f * ( (float)v / (float)( getVoiceCount() - 1 ) );
						}

						// Glide start pitch.
						float pitch = GlideManager::GetPitchSemitones() + detune;
//						_RPT1(_CRT_WARN, "START GLIDE %f\n", pitch);

						TuneKey(noteNumber, pitch, false, pinRetrigger != 0);

						// send note-on MIDI message with new key number (voice).
						NoteOn( MainNote, noteNumber, note.velocity );

						// Glide end pitch.
						pitch = MainNote + detune;
						TuneKey(noteNumber, pitch, true, false, 1);
//						_RPT1(_CRT_WARN, "TO ->GLIDE %f\n", pitch);

						// Pan voice.
						unsigned char cPan = (unsigned char)pan;
						if (cPan != ccPan[noteNumber])
						{
							ccPan[noteNumber] = cPan;

															//        Device KBIC   BM   CHAN  KEY   CC    VAL   EOX         
							unsigned char panMessage[] = { 0xF0, 0x7F, 0x00, 0x0A, 0x01, 0x00, 0x00, 0x0A, 0x00, 0xF7 }; // 0x0A = Pan.
							panMessage[6] = noteNumber; // MIDI key number (remapped key).
							panMessage[8] = (int)pan;
							pinMIDIOut.send(panMessage, sizeof(panMessage));
						}
					}
				}
			}
		}
		break;

		case gmpi::midi_2_0::NoteOff:
		{
			sendThrough = false;
			const auto note = gmpi::midi_2_0::decodeNote(msg);
			int MainNote = note.noteNumber;
			/*
			_RPT1(_CRT_WARN, "NOTE_OFF %d\n", MainNote);
			_RPT0(_CRT_WARN, "Note Memory Before\n");
			int i = note_memory_idx;
			do
			{
				_RPT2(_CRT_WARN, "%d : %d\n", i, note_memory[i]);

				i = (i - 1) & (MCV_NOTE_MEM_SIZE - 1);
			} while (note_memory[i] != -1);
			*/

			note_status[MainNote] = false;

			if (pinMode == 1) // Mono.
			{
				if (note_memory[note_memory_idx] == MainNote)// Did we just release the most recent note. The one that is playing now?
				{
					// Finds most recent note still held.
					int replacementKey = -1;

					char previousKey;
					do
					{
						previousKey = note_memory[note_memory_idx];

						// Clear slot.
						note_memory[note_memory_idx] = -1;

						if (previousKey >= 0 && note_status[previousKey]) // we found a prev still held. Glide to it.
						{
//							_RPT1(_CRT_WARN, "Glide Back to %d\n", previousKey);

							replacementKey = previousKey;
							break;
						}

						// Previous key.
						note_memory_idx = (note_memory_idx - 1) & (MCV_NOTE_MEM_SIZE - 1);

					} while (previousKey >= 0);

					/* who cares, unlikely
					// can't find last note, revert to lowest note priority.
					if (previousKey == -1)
					{
						for (int n = 0; n < 128; ++n)
						{
							if (note_status[n])
							{
								previousKey = n;
								break;
							}
						}
					}
					*/

					if (previousKey > -1)
					{
						note_memory[note_memory_idx] = previousKey;
						MonoGlide(previousKey);
					}
					else
					{
						notesHeld = false;

						AllNotesOff();
					}
					/*
					_RPT0(_CRT_WARN, "Note Memory After\n");
					int i = note_memory_idx;
					do
					{
						_RPT2(_CRT_WARN, "%d : %d\n", i, note_memory[i]);

						i = (i - 1) & (MCV_NOTE_MEM_SIZE - 1);
					} while (note_memory[i] != -1);
					*/
				}
			}
			else // Poly.
			{
				notesHeld = false;

				for (int n = 0; n < 128; ++n)
				{
					if (note_status[n])
					{
						notesHeld = true;
					}
				}

				NoteOff(MainNote);
			}
		}
		break;

		case gmpi::midi_2_0::PolyAfterTouch:
		{
			sendThrough = false;

			const auto aftertouch = gmpi::midi_2_0::decodePolyController(msg);

//			unsigned char midiMessageOut[] = { GmpiMidi::MIDI_PolyAfterTouch, 0, static_cast<unsigned char>(b3) };
			int incommingKey = aftertouch.noteNumber;
			for (int n = 0; n < 128; ++n)
			{
				if (keyVoiceAssign[n] == incommingKey)
				{
//					midiMessageOut[1] = n;

					const auto out = gmpi::midi_2_0::makePolyPressure(
						n,
						aftertouch.value
					);

					//				_RPTN(0, "MPE: Pressure %d: %f\n", keyInfo.MidiKeyNumber, channelPressure[header.channel]);

					pinMIDIOut.send((const unsigned char*)&out, sizeof(out));
				}
			}
		}
		break;

		default:
			break;
	}

	if( sendThrough )
	{
		pinMIDIOut.send(msg.begin(), msg.size());
	}
}

void Unison::onSetPins(void)
{
	// Set processing method.
	SET_PROCESS(&Unison::subProcess);

	if (pinMode.isUpdated())
	{
		// Prevent stuck notes.
		AllNotesOff();
	}

	bool reTune = false;
	if (pinSpread.isUpdated())
	{
		reTune = true;
	}

	if (pinVoices.isUpdated())
	{
		reTune = true;

		if (pinMode == 0) // Poly
		{
			// Add or remove correct number of notes.
			for (int n = 0; n < midiVoiceCount; ++n)
			{
				if (note_status[n] )
				{
					int voiceCount = 0;
					for (int i = 0; i < midiVoiceCount; ++i)
					{
						if (keyVoiceAssign[i] == n)
						{
							// Turn off excess notes.
							if (voiceCount == getVoiceCount())
							{
//								unsigned char midiMessage[] = { GmpiMidi::MIDI_NoteOff, 0, 64 };
//								midiMessage[1] = i;
								const auto out = gmpi::midi_2_0::makeNoteOffMessage(
									i,
									0.5f
								);
								pinMIDIOut.send((const unsigned char*)&out, sizeof(out));
								keyVoiceAssign[i] = -1;
							}
							else
							{
								++voiceCount;
							}
						}
					}
					while (voiceCount < getVoiceCount())
					{
						for (int i = 0; i < midiVoiceCount; ++i)
						{
							if (keyVoiceAssign[i] == -1)
							{
								//unsigned char midiMessage[] = { GmpiMidi::MIDI_NoteOn, 0, 64 };
								//midiMessage[1] = i;
								//midiMessage[2] = noteVelocity[n];
								const auto out = gmpi::midi_2_0::makeNoteOnMessage(
									i,
									noteVelocity[n]
								);
								pinMIDIOut.send((const unsigned char*)&out, sizeof(out));
								keyVoiceAssign[i] = n;
								break;
							}
						}
						++voiceCount;
					}
				}
			}
		}
		else
		{
			int voiceCount = 0;
			for (int i = 0; i < midiVoiceCount; ++i)
			{
				if (keyVoiceAssign[i] != -1)
				{
					// Turn off excess notes.
					if (voiceCount == getVoiceCount())
					{
						//unsigned char midiMessage[] = { GmpiMidi::MIDI_NoteOff, 0, 64 };
						//midiMessage[1] = i;
						const auto out = gmpi::midi_2_0::makeNoteOffMessage(
							i,
							0.5f
						);
						pinMIDIOut.send((const unsigned char*)&out, sizeof(out));
						keyVoiceAssign[i] = -1;
					}
					else
					{
						++voiceCount;
					}
				}
			}
			while (voiceCount < getVoiceCount())
			{
				for (int i = 0; i < midiVoiceCount; ++i)
				{
					if (keyVoiceAssign[i] != -1)
					{
						int keyNum = note_memory[note_memory_idx];
						//unsigned char midiMessage[] = { GmpiMidi::MIDI_NoteOn, 0, 64 };
						//midiMessage[1] = i;
						//midiMessage[2] = noteVelocity[keyNum];
						const auto out = gmpi::midi_2_0::makeNoteOnMessage(
							i,
							noteVelocity[keyNum]
						);
						pinMIDIOut.send((const unsigned char*)&out, sizeof(out));
						keyVoiceAssign[i] = keyNum;
						break;
					}
				}
				++voiceCount;
			}

		}
	}

	if (reTune)
	{
		// Retune playing notes.
		if (pinMode == 0) // Poly
		{
			for (int n = 0; n < midiVoiceCount; ++n)
			{
				if (note_status[n])
				{
					int v = 0;
					float pitchSemitones = (float)n;
					for (int i = 0; i < midiVoiceCount; ++i)
					{
						if (keyVoiceAssign[i] == n)
						{
							float detune;
							if (getVoiceCount() == 1)
							{
								detune = 0.0f;
							}
							else
							{
								detune = pinSpread * ((float)v / (float)(getVoiceCount() - 1) - 0.5f);
							}
							TuneKey(i, pitchSemitones + detune);
							++v;
						}
					}
				}
			}
		}
		else
		{
			int n = note_memory[note_memory_idx];
			int v = 0;
			float pitchSemitones = (float)n;
			for (int i = 0; i < midiVoiceCount; ++i)
			{
				if (keyVoiceAssign[i] != -1)
				{
					float detune;
					if (getVoiceCount() == 1)
					{
						detune = 0.0f;
					}
					else
					{
						detune = pinSpread * ((float)v / (float)(getVoiceCount() - 1) - 0.5f);
					}
					TuneKey(i, pitchSemitones + detune);
					++v;
				}
			}
		}
	}
	setSleep(false);
}

void Unison::TuneKey(int keyNumber, float tunef, bool glide, bool retrigger, int timeDelay)
{
	if (outTuning[keyNumber] == tunef)
	{
		return;
	}
	outTuning[keyNumber] = tunef;

	int timestamp = getBlockPosition() + timeDelay; // naughty

	// Turn on glide. Poly CC 65 = Portamento On/Off.
	/*
	Key-Based Instrument Controller message. (Polyphonic controllers).
	F0 7F Universal Real Time SysEx header
	<device ID> ID of target device (7F = all devices)
	0A sub-ID#1 = “Key-Based Instrument Control”
	01 sub-ID#2 = 01 Basic Message
	0n MIDI Channel Number
	kk Key number
	[nn,vv] Controller Number and Value
	:
	F7 EOX
	*/
	int v = 0; // bit 7 = glide. bit 1 = retrigger.
	if (glide)
	{
		v = 0x40;
	}
	if (retrigger)
	{
		v |= 0x20;
	}
	if (v != ccGlide[keyNumber])
	{
		ccGlide[keyNumber] = v;
										//                 KBIC   BM   CHAN  KEY   CC    VAL   EOX         
		unsigned char glideMessage[] = { 0xF0, 0x7F, 0x00, 0x0A, 0x01, 0x00, 0x00, 0x41, 0x00, 0xF7 };
		glideMessage[6] = keyNumber; // MIDI key number (remapped key).
		glideMessage[8] = v;
		pinMIDIOut.send(glideMessage, sizeof(glideMessage), timestamp);
	}

/*
	[SINGLE NOTE TUNING CHANGE (REAL-TIME)]

	F0 7F <device ID> 08 02 tt ll [kk xx yy zz] F7

	F0 7F  Universal Real Time SysEx header
	<device ID>  ID of target device
	08  sub-ID#1 (MIDI Tuning)
	02  sub-ID#2 (note change)
	tt  tuning program number (0 – 127)
	ll  number of changes (1 change = 1 set of [kk xx yy zz])
	[kk]  MIDI key number
	[xx yy zz]  frequency data for that key (repeated ‘ll' number of times)
	F7  EOX
*/
#if 0
	// send tuning MIDI message for new key number (voice).
	unsigned char tuneMessage[] = { 0xF0, 0x7F, 0x00, 0x08, 0x02, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0xF7 };
	tuneMessage[7] = keyNumber; // MIDI key number (remapped key).
	int tune = (int)(tunef * (float)0x4000);
	
	if (tune < 0)
		tune = 0;
	if (tune > 0x1fc000)
		tune = 0x1fc000;

	tuneMessage[8] = tune >> 14; // tuning course.
	tuneMessage[9] = (tune >> 7) & 0x7f; // tuning fine.
	tuneMessage[10] = tune & 0x7f; // tuning fine.

	pinMIDIOut.send(tuneMessage, sizeof(tuneMessage), timestamp);
#endif
	const auto out = gmpi::midi_2_0::makePolyController(
		keyNumber,
		gmpi::midi_2_0::PolyPitch,
		tunef
	);
	pinMIDIOut.send((const unsigned char*)&out, sizeof(out));

//	_RPT2(_CRT_WARN, "%d : %f\n", keyNumber, (float)tune / (float)0x4000);
//	fprintf(stderr, "UNISON: key: %d  tune: %f\n", keyNumber, (float)tune / (float)0x4000);
}

void Unison::MonoGlide(int MainNote)
{
	GlideManager::GlideToSemitone((float)MainNote, pinPortamentoTime.getValue(), pinPortamentoMode, getSampleRate());

//TODO	setSleep(GlideManager::isActive());

	int v = 0;
	for (int noteNumber = 0; noteNumber < 128; ++noteNumber)
	{
		if (keyVoiceAssign[noteNumber] != -1)
		{
			float detune = 0.0f;
			if (getVoiceCount() > 1)
			{
				detune = pinSpread * ((float)v / (float)(getVoiceCount() - 1) - 0.5f);
			}

			float pitch = MainNote + detune;
			TuneKey(noteNumber, pitch, true, pinRetrigger != 0);

			++v;
		}
	}
}

void Unison::subProcess(int bufferOffset, int sampleFrames)
{
	setSleep(GlideManager::process(sampleFrames));
}

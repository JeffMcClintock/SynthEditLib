#include "../se_sdk3/mp_sdk_audio.h"
#include "../se_sdk3/mp_midi.h"
#include <sstream>
using namespace gmpi;
using namespace GmpiMidi;

namespace
{
	// Length of a MIDI 1.0 message, given its status byte.
	// Returns 0 for System Exclusive, which has no fixed length.
	int midiMessageSize(unsigned char status)
	{
		switch(status & 0xF0)
		{
		case MIDI_NoteOff:
		case MIDI_NoteOn:
		case MIDI_PolyAfterTouch:
		case MIDI_ControlChange:
		case MIDI_PitchBend:
			return 3;

		case MIDI_ProgramChange:
		case MIDI_ChannelPressue:
			return 2;
		}

		switch(status) // System messages.
		{
		case MIDI_SystemMessage: // System Exclusive, runs until the terminating F7.
			return 0;

		case 0xF1: // MIDI Time Code Quarter Frame.
		case 0xF3: // Song Select.
			return 2;

		case 0xF2: // Song Position Pointer.
			return 3;
		}

		return 1; // Tune Request, End-of-SysEx, and all the System Real-Time messages.
	}
}

class MidiMessage : public MpBase2
{
	StringInPin pinHexBytes;
	BoolInPin pinTrigger;
	MidiOutPin pinMidiOut;

public:
	MidiMessage()
	{
		initializePin( pinHexBytes );
		initializePin( pinTrigger );
		initializePin( pinMidiOut );
	}

	void onSetPins() override
	{
		if(pinTrigger.isUpdated() && pinTrigger.getValue())
		{
			std::wstringstream ss(pinHexBytes.getValue());
			
			int idx{};
			const int maxSize = 1000;
			unsigned char midiMessage[maxSize];
			while(ss.good() && idx < maxSize)
			{
				std::wstring substr;
				std::getline(ss, substr, L',');
				
				unsigned char byte{};
				int nybble{};
				for(auto c : substr)
				{
					c = tolower(c);

					if(c >= L'0' && c <= L'9')
					{
						byte = (byte << 4) | (c - L'0');
						++nybble;
					}
					if(c >= L'a' && c <= L'f')
					{
						byte = (byte << 4) | (c - L'a' + 10);
						++nybble;
					}
					if(nybble == 2)
					{
						nybble = 0;
						midiMessage[idx++] = byte;
					}
				}
				if (nybble == 1) // trailing byte was 1 char only, pass it out.
				{
					midiMessage[idx++] = byte;
				}
			}

			// for large string, break into individual messages.
			int pos = 0;
			unsigned char runningStatus{};

			while(pos < idx)
			{
				const bool isStatusByte = (midiMessage[pos] & 0x80) != 0;
				const unsigned char status = isStatusByte ? midiMessage[pos] : runningStatus;

				if((status & 0x80) == 0) // a data byte with no status byte to belong to. skip it.
				{
					++pos;
					continue;
				}

				if(isStatusByte && status < 0xF8) // System Real-Time messages don't disturb running status.
				{
					runningStatus = status < MIDI_SystemMessage ? status : 0; // System Common cancels running status.
				}

				int size = midiMessageSize(status);

				if(size == 0) // System Exclusive. Extends to the terminating F7, or to the end of the data.
				{
					size = 1;
					while(pos + size < idx && midiMessage[pos + size] != MIDI_SystemMessageEnd)
					{
						++size;
					}

					if(pos + size < idx)
					{
						++size; // include the F7.
					}
				}

				if(isStatusByte)
				{
					size = (std::min)(size, idx - pos); // truncated message, send what we have.

					pinMidiOut.send(midiMessage + pos, size);
					pos += size;
				}
				else
				{
					// Running status. Re-insert the implied status byte.
					const int dataBytes = (std::min)(size - 1, idx - pos);

					unsigned char temp[3] = { status };
					for(int i = 0; i < dataBytes; ++i)
					{
						temp[1 + i] = midiMessage[pos + i];
					}

					pinMidiOut.send(temp, dataBytes + 1);
					pos += dataBytes;
				}
			}
		}
	}
};

namespace
{
	auto r = sesdk::Register<MidiMessage>::withId(L"SE MidiMessage");
}

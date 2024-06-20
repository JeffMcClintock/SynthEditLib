/* Copyright (c) 2007-2022 SynthEdit Ltd
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*     * Redistributions of source code must retain the above copyright
*       notice, this list of conditions and the following disclaimer.
*     * Redistributions in binary form must reproduce the above copyright
*       notice, this list of conditions and the following disclaimer in the
*       documentation and/or other materials provided with the distribution.
*     * Neither the name SEM, nor SynthEdit, nor 'Music Plugin Interface' nor the
*       names of its contributors may be used to endorse or promote products
*       derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY SynthEdit Ltd ``AS IS'' AND ANY
* EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL SynthEdit Ltd BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "mp_sdk_audio.h"
#include "mp_midi.h"

using namespace gmpi;

#pragma pack(push, 1)
typedef char chunk_name_t[4];

struct midi_file_header
{
	chunk_name_t chunk_name = {0x64, 0x68, 0x54, 0x4D}; // MThd
	uint32_t chunk_size = 6;
	uint16_t format = 0;
	uint16_t ntracks = 1;
	uint16_t division = 96;
};


struct midi_track_header
{
	chunk_name_t chunk_name = { 0x6B,0x72, 0x54, 0x4D }; // MTrk
	uint32_t chunk_size = 0;
};

#pragma pack(pop)

#if 0
void write(FILE* f, chunk_name_t& chunk)
{
	assert(f);

	for (int i = 3; i >= 0; --i)
	{
		fwrite(&chunk[i], 1, 1, f);
	}
}
#endif

template<typename T>
void write(FILE* f, const T& v)
{
	assert(f);

	const char* bytes = reinterpret_cast<const char*>( &v );

	for (int i = sizeof(v) - 1; i >= 0; --i)
	{
		fwrite(&bytes[i], 1, 1, f);
	}
}

class MIDIRecorder final : public MpBase2
{
	FloatInPin pinHostBpm;
	FloatInPin pinHostSongPosition;
	FloatInPin pinHostBarStart;
	IntInPin pinNumerator;
	IntInPin pinDenominator;

	StringInPin pinFileName;
	BoolInPin pinTrigger;
	BoolInPin pinGate;
	MidiInPin pinMidiIn;
	FloatInPin pinHostBPM;
	IntInPin pinTempofrom;
	BoolInPin pinLoopMode;

	double ticksPerSampleframe = 0.0;
	double ticks = 0.0;
	int lastSentTick = 0;
	const double ppqn = 192.0; // pulses per quarter note
	FILE* outputStream = {};

	gmpi::midi_2_0::MidiConverter1 midiConverter; // to MIDI 1.0

public:
	MIDIRecorder() :
		midiConverter(
			// provide a lambda to accept converted MIDI 2.0 messages
			[this](const midi::message_view& msg, int offset) {
				onMidi1(msg, offset);
			}
		)
	{
		initializePin(pinHostBpm);
		initializePin(pinHostSongPosition);
		initializePin(pinHostBarStart);
		initializePin(pinNumerator);
		initializePin(pinDenominator);

		initializePin( pinFileName );
		initializePin( pinTrigger );
		initializePin( pinGate );
		initializePin( pinMidiIn );
	}
	
	~MIDIRecorder()
	{
		closeFile();
	}

	void subProcess( int sampleFrames )
	{
		ticks += sampleFrames * ticksPerSampleframe;
	}
	
	void WriteVarLen(FILE* file, int32_t value) const
	{
		int32_t buffer;
		buffer = value & 0x7f;
		while ((value >>= 7) > 0)
		{
			buffer <<= 8;
			buffer |= 0x80;
			buffer += (value & 0x7f);
		}
		while (true)
		{
			putc(buffer, file);
			if (buffer & 0x80)
				buffer >>= 8;
			else
				break;
		}
	}

	void onMidi1(const midi::message_view& msg, int offset)
	{
		if (!outputStream)
			return;

		const auto thisTick = static_cast<int32_t>(ticks + 0.5);

		// write tick (offset)
		WriteVarLen(outputStream, thisTick - lastSentTick);

		switch (msg[0])
		{
		case 0xF0: // SYSEX
			fwrite(msg.begin(), 1, 1, outputStream); // write F0
			// write length
			WriteVarLen(outputStream, msg.size() - 1);
			//write SYSex bytes
			fwrite(msg.begin() + 1, 1, msg.size() - 1, outputStream);
			lastSentTick = thisTick;
			break;

		default:
			// normal notes etc
			if (msg.size() > 1)
			{
				fwrite(msg.begin(), 1, msg.size(), outputStream);
				lastSentTick = thisTick;
			}
			break;
		}
	}

	void onSetPins() override
	{
		if (pinHostBpm.isUpdated())
		{
			const double pp_sec = (pinHostBpm.getValue() / 60.0) * ppqn;
			ticksPerSampleframe = pp_sec / getSampleRate();
		}

		// Check which pins are updated.
		if( pinFileName.isUpdated() )
		{
		}

		if( pinTrigger.isUpdated() && pinTrigger.getValue() && pinGate.getValue())
		{
			closeFile();

			std::wstring fname = pinFileName;
			if (fname.find(L".mid") == std::string::npos && fname.find(L".MID") == std::string::npos)
			{
				fname += L".mid";
			}

			const auto fullFilename = host.resolveFilename(fname);

			outputStream = fopen(fullFilename.c_str(), "wb");

			if (!outputStream)
			{
#ifdef _WIN32
				MessageBoxA(0, "MIDI Recorder: Failed to open output file.", "debug msg", MB_OK);
#endif
				return;
			}


			midi_file_header header;
			header.division = static_cast<uint16_t>(ppqn);

			// write the header chunk.
			write(outputStream, header.chunk_name);
			write(outputStream, header.chunk_size);
			write(outputStream, header.format);
			write(outputStream, header.ntracks);
			write(outputStream, header.division);

			// write the track chunk header.

			midi_track_header track_header;

#if 0
			if (fwrite(&track_header, 1, sizeof(track_header), outputStream) != sizeof(track_header))
			{
				//error
				fclose(outputStream);
				outputStream = {};
		}
#endif
			write(outputStream, track_header.chunk_name);
			write(outputStream, track_header.chunk_size);

			ticks = 0.0;
			lastSentTick = 0;
#if 0
			// write time signature and tempo
			{
				const auto thisTick = static_cast<int32_t>(ticks);
				// write tick (offset)
				WriteVarLen(outputStream, thisTick - lastSentTick);

				unsigned char msg[] = { 0xFF, 0x51, 0x00, 0x03, 0x07, 0xA1, 0x20 }; // tempo
				fwrite(&msg, 1, sizeof(msg), outputStream);
	}
#endif

		}
		if( pinGate.isUpdated() && !pinGate.getValue())
		{
			closeFile();
		}

		setSleep(outputStream == nullptr);

		// Set processing method.
		setSubProcess(&MIDIRecorder::subProcess);
	}

	void closeFile()
	{
		if (!outputStream)
			return;

		// write 'end of track'
		{
			const auto thisTick = static_cast<int32_t>(ticks);
			// write tick (offset)
			WriteVarLen(outputStream, thisTick - lastSentTick);

			unsigned char endoftrack[] = { 0xFF, 0x2F, 0x00 }; // end of track
			fwrite(&endoftrack, 1, sizeof(endoftrack), outputStream);
		}

		// rewind and rewrite track header to include correct length
		midi_track_header track_header;
		track_header.chunk_size = static_cast<uint32_t>(ftell(outputStream) - sizeof(midi_file_header) - sizeof(track_header));

		// update track header to correct number of bytes written
		fseek(outputStream, sizeof(midi_file_header), SEEK_SET);

		write(outputStream, track_header.chunk_name);
		write(outputStream, track_header.chunk_size);

		fclose(outputStream);
		outputStream = {};
	}

	// MIDI 2.0 to MIDI 1.0 conversion.
	void onMidiMessage(int pin, const unsigned char* midiMessage, int size) override // size < 4 for short msg, or > 4 for sysex.
	{
		// not interested in MIDI clocks
		if (size < 2)
			return;

		midi::message_view msg(midiMessage, size);
		midiConverter.processMidi(msg, -1);
	}
};

namespace
{
	auto r = Register<MIDIRecorder>::withId(L"SE MIDI Recorder");
}

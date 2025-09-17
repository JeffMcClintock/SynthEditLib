/* Copyright (c) 2007-2023 SynthEdit Ltd

Permission to use, copy, modify, and /or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS.IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
#include "mp_sdk_audio.h"
#include <limits>

using namespace gmpi;

class BadVoltage final : public MpBase2
{
	IntInPin pinMode;
	AudioOutPin pinOutput;

	float value = 0.0f;

public:
	BadVoltage()
	{
		initializePin(pinMode);
		initializePin( pinOutput );
	}

	void subProcess( int sampleFrames )
	{
		// get pointers to in/output buffers.
		auto streaming = getBuffer(pinOutput);

		for( int s = sampleFrames; s > 0; --s )
		{
			// TODO: Signal processing goes here.
			*streaming = value;
			// Increment buffer pointers.
			++streaming;
		}
	}

	void onSetPins() override
	{
		switch( pinMode )
		{
		case 0:
			value = std::numeric_limits<double>::quiet_NaN();
			break;
		case 1:
			value = std::numeric_limits<double>::infinity();
			break;
		case 2:
			value = -std::numeric_limits<double>::infinity();
			break;
		default:
			value = 0.0f;
		}

		// Set state of output audio pins.
		pinOutput.setStreaming(false);

		// Set processing method.
		setSubProcess(&BadVoltage::subProcess);
	}
};

namespace
{
	auto r = sesdk::Register<BadVoltage>::withId(L"SE Bad Voltage");
}

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

using namespace gmpi;

class BlobOverloaderDSP final : public MpBase2
{
	IntInPin pinMode;
	BlobOutPin pinOut;

public:
	BlobOverloaderDSP()
	{
		initializePin( pinMode );
		initializePin( pinOut );
	}

	void onSetPins() override
	{
		// Check which pins are updated.
		if( pinMode.isUpdated() )
		{

			std::vector<char> bytes;
			bytes.assign(pinMode.getValue() * 1024 * 1024, 0xde);

			MpBlob big(bytes.size(), bytes.data());
			pinOut = big;
		}
	}
};

namespace
{
	auto r = Register<BlobOverloaderDSP>::withId(L"SE Blob Overloader (DSP)");
}

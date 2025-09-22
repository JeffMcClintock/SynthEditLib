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
#include "mp_sdk_gui2.h"

using namespace gmpi;

class BlobSizeGui final : public SeGuiInvisibleBase
{
 	void onSetValueIn()
	{
		pinValueOut = static_cast<int>(pinValueIn.getValue().getSize());
	}

 	BlobGuiPin pinValueIn;
 	IntGuiPin pinValueOut;

public:
	BlobSizeGui()
	{
		initializePin( pinValueIn, static_cast<MpGuiBaseMemberPtr2>(&BlobSizeGui::onSetValueIn) );
		initializePin( pinValueOut );
	}
};

namespace
{
	auto r = Register<BlobSizeGui>::withId(L"SE Blob Size");
}

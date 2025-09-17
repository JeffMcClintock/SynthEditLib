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

class BlobOverloaderGUIGui final : public SeGuiInvisibleBase
{
 	void onSetBlobsizeMB()
	{
		std::vector<char> bytes;
		bytes.assign(pinBlobsizeMB * 1024 * 1024, 0xde);

		MpBlob big(bytes.size(), bytes.data());
		pinValueIn = big;
	}

 	BlobGuiPin pinValueIn;
 	IntGuiPin pinBlobsizeMB;

public:
	BlobOverloaderGUIGui()
	{
		initializePin( pinValueIn);
		initializePin( pinBlobsizeMB, static_cast<MpGuiBaseMemberPtr2>(&BlobOverloaderGUIGui::onSetBlobsizeMB) );
	}
};

namespace
{
	auto r = Register<BlobOverloaderGUIGui>::withId(L"SE Blob overloader (GUI)");
}

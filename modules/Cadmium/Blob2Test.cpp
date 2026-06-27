/* Copyright (c) 2007-2026 SynthEdit Ltd

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

// Modern (GMPI SDK) port of the legacy se_sdk3 Blob2Test. Exercises the 'object'
// pin datatype: a reference-counted COM object (api::ISharedBlob) passed between
// modules by pointer rather than by value.

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include "Processor.h"
#include "Helpers/SharedBlob.h"

// The GMPI SDK has no equivalent of SE_DECLARE_INIT_STATIC_FILE; provide a no-op
// so static-library linkage matches the other modules.
#ifndef SE_DECLARE_INIT_STATIC_FILE
#define SE_DECLARE_INIT_STATIC_FILE(filename) void se_static_library_init_##filename(){}
#endif

SE_DECLARE_INIT_STATIC_FILE(Blob2Test)

using namespace gmpi;

class Blob2Test final : public Processor
{
	BoolInPin                      pinTrigger;
	ObjectInPin<api::ISharedBlob>  pinValueIn;
	ObjectOutPin<api::ISharedBlob> pinValueOut;

	int value = 1234;

	// A small pool of output blobs plus their backing storage. A view can't be
	// recycled while a downstream module still references it (inUse()).
	SharedBlobView outputBlobPool[2];
	char outputValues[2][100];

public:
	void onSetPins() override
	{
		// An object arrived on the input pin.
		if (pinValueIn.isUpdated())
		{
			// The pin already resolved the ISharedBlob interface for us - no queryInterface
			// boilerplate, and no unsafe assumption: getValue() is null if the object that
			// arrived isn't an ISharedBlob (or there's no object at all).
			if (auto blob = pinValueIn.getValue())
			{
				const uint8_t* data{};
				int64_t size{};
				if (blob->read(&data, &size) == ReturnCode::Ok)
				{
					// print the blob contents for diagnostic purposes.
					std::fprintf(stderr, "Blob2Test: received ISharedBlob %p, %lld bytes: ",
						static_cast<void*>(pinValueIn.getRawObject()), static_cast<long long>(size));
					for (int64_t i = 0; i < size; ++i)
						std::fprintf(stderr, "%c", static_cast<int>(data[i]));
					std::fprintf(stderr, "\n");
				}
			}
			else if (pinValueIn.getRawObject())
			{
				// Some other kind of object - we don't know how to read it, and that's fine.
				std::fprintf(stderr, "Blob2Test: received an object that is not an ISharedBlob\n");
			}
			else
			{
				std::fprintf(stderr, "Blob2Test: received null object\n");
			}

			// Forward the object downstream unchanged (pointer only, no copy).
			pinValueOut = pinValueIn;
		}

		// When triggered, allocate a fresh blob from the pool and send it out.
		if (pinTrigger.isUpdated() && pinTrigger.getValue())
		{
			bool sent = false;
			for (int i = 0; i < static_cast<int>(std::size(outputBlobPool)); ++i)
			{
				if (outputBlobPool[i].inUse())
					continue;

				// write the data (ASCII of an incrementing integer) into the backing store.
				const std::string text = std::to_string(value);
				const size_t len = (std::min)(text.size(), sizeof(outputValues[i]) - 1);
				std::memcpy(outputValues[i], text.data(), len);
				outputValues[i][len] = '\0';

				// point the view at the backing store, then send it out.
				outputBlobPool[i].set(outputValues[i], static_cast<int64_t>(len));
				std::fprintf(stderr, "Blob2Test: sending ISharedBlob %p = \"%s\"\n",
					static_cast<void*>(&outputBlobPool[i]), outputValues[i]);
				pinValueOut = &outputBlobPool[i];
				sent = true;
				break;
			}

			if (!sent)
				std::fprintf(stderr, "Blob2Test: FAILED to allocate a spare blob!\n");

			++value;
		}
	}
};

namespace
{
	auto r = gmpi::Register<Blob2Test>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<PluginList>
    <Plugin id="SE Blob2 Test2" name="Blob2 Test2" category="Debug">
        <Audio>
            <!-- non-sequential pin IDs are intentional (tests id-based pin mapping) -->
            <Pin id="1" name="Trigger" datatype="bool"/>
            <Pin id="3" name="Object" datatype="object:blob"/>
            <Pin id="99999" name="Object" datatype="object:blob" direction="out"/>
        </Audio>
    </Plugin>
</PluginList>
)XML");
}

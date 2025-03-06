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
#include "SharedBlob.h"
#include "mfc_emulation.h"

using namespace gmpi;

SE_DECLARE_INIT_STATIC_FILE(Blob2Test)

class Blob2Test final : public MpBase2
{
	BoolInPin pinTrigger;
	Blob2InPin pinValueIn;
	Blob2OutPin pinValueOut;

	int value = 1234;

	gmpi::ISharedBlob* inputBlob = {};
	SharedBlobView outputBlobPool[2];
	char outputValues[2][100];

public:
	Blob2Test()
	{
		initializePin(1, pinTrigger);
		initializePin(3, pinValueIn);
		initializePin(99999, pinValueOut);
	}

	void onSetPins() override
	{
		// Check which pins are updated.
		if (pinValueIn.isUpdated())
		{
			inputBlob = pinValueIn.getValue();

			// print contents of BLOB for diagnostic purposes
			_RPTN(0, "Recieved BLOB2 at %x\n", inputBlob);
			uint8_t* data{};
			int64_t size{};
			if (inputBlob) // blob can be null
			{
				if (gmpi::MP_OK == inputBlob->read(&data, &size))
				{
					// print the BLOB
					for (int i = 0; i < size; ++i)
					{
						_RPT1(0, "%c ", (int)data[i]);
					}
					_RPT0(0, "\n");
				}
			}

			// Pass a BLOB unchanged to an output pin.
			pinValueOut = pinValueIn;

// or, Allocate and send a new blob
#if 0
			// allocate output blob
			bool sent = false;
			for(int i = 0; i < std::size(outputBlobPool) ; ++i)
			{
				if (!outputBlobPool[i].inUse())
				{
					strncpy(outputValues[i], (const char*) data, (std::min)(std::size(outputValues[0]), static_cast<size_t>(size)));

					pinValueOut = &outputBlobPool[i];
					sent = true;
					break;
				}
			}
			if (!sent)
			{
				_RPT0(0, "FAILED to allocate spare BLOB!!\n");
			}
#endif
		}

		// When triggered, send a blob.
		if (pinTrigger.isUpdated() && pinTrigger.getValue())
		{
			bool sent = false;
			for (int i = 0; i < std::size(outputBlobPool); ++i)
			{
				auto blob_ptr = &outputBlobPool[i];
				if (!blob_ptr->inUse())
				{
					// write new data to output blob (ASCII representation of incrementing integer)
					// warning: assumes there is enough room in buffer
//					_itoa(value, outputValues[i], 10);
					const std::string valueStr = std::to_string(value);
					std::strncpy(outputValues[i], valueStr.c_str(), sizeof(outputValues[i]) - 1);
					outputValues[i][sizeof(outputValues[i]) - 1] = '\0'; // Ensure null-termination

					// associate the memory with the blob view
					blob_ptr->set(outputValues[i], strlen(outputValues[i]));

					// send my blob out
					_RPTN(0, "Sending BLOB2 at %x\n", blob_ptr);
					pinValueOut = blob_ptr;
					sent = true;
					break;
				}
			}

			if (!sent)
			{
				_RPT0(0, "FAILED to allocate spare BLOB!!\n");
			}

			value++;
		}
	}
};

namespace
{
	auto r = sesdk::Register<Blob2Test>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<PluginList>
    <Plugin id="SE Blob2 Test" name="Blob2 Test" category="Debug">
        <Audio>
			<!-- TESTING NON-SEQUENTIAL PIN IDs -->
            <Pin id="1" name="Trigger" datatype="bool"/>
            <Pin id="3" name="Blob2" datatype="blob2"/>
            <Pin id="99999" name="Blob2" datatype="blob2" direction="out"/>
        </Audio>
    </Plugin>
</PluginList>
)XML");
}

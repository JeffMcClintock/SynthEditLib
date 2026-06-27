/* Copyright (c) 2007-2025 SynthEdit Ltd

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
#include <memory>
#include <string>
#include "Processor.h"
#include "Extensions/PinCount.h"
#include "Helpers/SharedBlob.h"

using namespace gmpi;

struct FirstNonEmpty final : public Processor
{
	StringOutPin pintextOut;
	std::vector< std::unique_ptr<StringInPin> > inputPins;

	FirstNonEmpty() = default;

	gmpi::ReturnCode open(api::IUnknown* phost) override
	{
		synthedit::PinInformation info(phost);

		for (auto& pin : info.pins)
		{
			if(pin.direction == PinDirection::In && pin.datatype == PinDatatype::String)
			{
				inputPins.push_back(std::make_unique<StringInPin>());
				init(*inputPins.back().get());
			}
		}

		return Processor::open(phost);
	}

	void onSetPins() override
	{
		for (auto& pin : inputPins)
		{
			auto v = pin->getValue();
			if (!v.empty())
			{
				pintextOut = v;
				break;
			}
		}
	}
};

namespace
{
auto r = Register<FirstNonEmpty>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE First Non Empty" name="First Non Empty" category="Conversion/String">
    <Audio>
        <Pin name="text" datatype="string" direction="out"/>
        <Pin name="text" datatype="string" autoDuplicate="true"/>
    </Audio>
</Plugin>
)XML");
}


struct StringConcat final : public Processor
{
	StringOutPin pintextOut;
	std::vector< std::unique_ptr<StringInPin> > inputPins;

	StringConcat() = default;

	gmpi::ReturnCode open(api::IUnknown* phost) override
	{
		synthedit::PinInformation info(phost);

		for (auto& pin : info.pins)
		{
			if (pin.direction == PinDirection::In && pin.datatype == PinDatatype::String)
			{
				inputPins.push_back(std::make_unique<StringInPin>());
				init(*inputPins.back().get());
			}
		}

		return Processor::open(phost);
	}

	void onSetPins() override
	{
		std::string result;
		for (auto& pin : inputPins)
		{
			result += pin->getValue();
		}
		pintextOut = result;
	}
};

namespace
{
auto r3 = Register<StringConcat>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE String Concat" name="String Concat" category="Conversion/String">
    <Audio>
        <Pin name="text" datatype="string" direction="out"/>
        <Pin name="text" datatype="string" autoDuplicate="true"/>
    </Audio>
</Plugin>
)XML");
}


struct OsDetect final : public Processor
{
	BoolOutPin pinIsWindows;
	BoolOutPin pinIsMac;

	OsDetect() = default;

	void onGraphStart() override	// called on very first sample.
	{
#ifdef _WIN32
		pinIsWindows = true;
		pinIsMac = false;
#else
		pinIsWindows = false;
		pinIsMac = true;
#endif
		Processor::onGraphStart();
	}
};

namespace
{
auto r2 = Register<OsDetect>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Operating System" name="Operating System Detect" category="Special">
    <Audio>
        <Pin name="Windows" datatype="bool" direction="out"/>
        <Pin name="macOS" datatype="bool" direction="out"/>
    </Audio>
</Plugin>
)XML");
}


// Convert a by-value Blob into a shared 'object' (api::ISharedBlob), passed downstream by reference.
struct BlobToObject final : public Processor
{
	BlobInPin                      pinValueIn;
	ObjectOutPin<api::ISharedBlob> pinValueOut;

	// Each sent blob must keep a stable address while it's still 'in flight' (referenced
	// downstream), so the pool holds unique_ptrs - growing the vector never moves existing entries.
	struct blobInfo
	{
		SharedBlobView blobview;
		std::string    blobData;
	};
	std::vector<std::unique_ptr<blobInfo>> outputBlobPool;

	void onSetPins() override
	{
		if (!pinValueIn.isUpdated())
			return;

		// find a free pool entry, or grow the pool.
		blobInfo* blob = nullptr;
		for (auto& entry : outputBlobPool)
		{
			if (!entry->blobview.inUse())
			{
				blob = entry.get();
				break;
			}
		}
		if (!blob)
		{
			outputBlobPool.push_back(std::make_unique<blobInfo>());
			blob = outputBlobPool.back().get();
		}

		// copy the incoming bytes into stable storage, point the view at them, and send.
		const Blob& in = pinValueIn.getValue();
		blob->blobData.assign(reinterpret_cast<const char*>(in.data()), in.size());
		blob->blobview.set(blob->blobData.data(), static_cast<int64_t>(blob->blobData.size()));

		pinValueOut = &blob->blobview;
	}
};

namespace
{
auto r4 = Register<BlobToObject>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE BlobToBlob2_b" name="Blob To Blob2 b" category="Conversion">
    <Audio>
        <Pin name="Blob Val" direction="in" datatype="blob"/>
        <Pin name="Blob2 Val" direction="out" datatype="object:blob"/>
    </Audio>
</Plugin>
)XML");
}


// Convert a shared 'object' (api::ISharedBlob) back into a by-value Blob.
struct ObjectToBlob final : public Processor
{
	ObjectInPin<api::ISharedBlob> pinValueIn;
	BlobOutPin                    pinValueOut;

	void onSetPins() override
	{
		if (!pinValueIn.isUpdated())
			return;

		// The pin resolved the ISharedBlob interface for us (null if the object is absent
		// or isn't an ISharedBlob) - no queryInterface boilerplate, no unsafe assumption.
		if (auto blob = pinValueIn.getValue())
		{
			const uint8_t* data{};
			int64_t size{};
			if (blob->read(&data, &size) == ReturnCode::Ok)
			{
				pinValueOut = Blob(data, data + size);
				return;
			}
		}

		// no usable object - emit an empty blob.
		pinValueOut = Blob{};
	}
};

namespace
{
auto r5 = Register<ObjectToBlob>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Blob2ToBlob_b" name="Blob2 To Blob b" category="Conversion">
    <Audio>
        <Pin name="Blob2 Val" direction="in" datatype="object:blob"/>
        <Pin name="Blob Val" direction="out" datatype="blob"/>
    </Audio>
</Plugin>
)XML");
}
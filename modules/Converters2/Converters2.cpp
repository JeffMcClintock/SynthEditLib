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
#include "Processor.h"
#include "Extensions/PinCount.h"

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

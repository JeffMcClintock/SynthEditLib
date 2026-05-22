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
#include "helpers/GmpiPluginEditor.h"
#include "Extensions/PinCount.h"

using namespace gmpi;
using namespace gmpi::editor;

class StringConcatGui final : public PluginEditorNoGui
{
	Pin<std::string> pintextOut;
	std::vector< std::unique_ptr< Pin<std::string> > > inputPins;

	void update()
	{
		std::string result;
		for(auto& p : inputPins)
			result += p->value;
		pintextOut = result;
	}

public:
	ReturnCode initialize() override
	{
		// figure out how many autoduplicating pins, and init them.
		synthedit::PinInformation info(editorHost);

		const auto inputPinCount = static_cast<int>(info.pins.size()) - 1;
		inputPins.reserve(inputPinCount);

		for(int i = 0; i < inputPinCount; ++i)
		{
			inputPins.push_back(std::make_unique< Pin<std::string> >());
			auto& pin = *inputPins.back();
			pin.host = editorHost.get();
			init(pin);

			pin.onUpdate = [this](PinBase*)
				{
					update();
				};
		}

		return PluginEditorNoGui::initialize();
	}
};

namespace
{
auto r = Register<StringConcatGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE String Concat GUI" name="String Concat GUI" category="Conversion/String">
    <GUI>
        <Pin name="text" datatype="string" direction="out"/>
        <Pin name="text" datatype="string" autoDuplicate="true"/>
    </GUI>
</Plugin>
)XML");
}

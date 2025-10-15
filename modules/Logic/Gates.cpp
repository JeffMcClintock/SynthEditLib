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
#include "Processor.h"

using namespace gmpi;

struct NOTGate final : public Processor
{
	BoolInPin input;
	BoolOutPin output;

	void onSetPins() override
	{
		output = !input.getValue();
	}
};

namespace
{
auto r = Register<NOTGate>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<PluginList>
    <Plugin id="SE NOT Gate" name="NOT Gate" category="Logic">
        <Audio>
            <Pin name="Signal in" datatype="bool"/>
            <Pin name="Signal Out" datatype="bool" direction="out"/>
        </Audio>
    </Plugin>
</PluginList>
)XML");
}

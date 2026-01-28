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

/*
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include "Windows.h"
#endif
*/

using namespace gmpi;

struct EnvironmentVariable final : public Processor
{
    StringInPin pinEnvironmentVariableName;
    StringOutPin pinEnvironmentVariableValue;

	void onSetPins() override
	{
		const auto name = pinEnvironmentVariableName.getValue();
        if (name.empty())
        {
            pinEnvironmentVariableValue = "";
            return;
        }

#if 0 //def _WIN32
        auto needed = GetEnvironmentVariableA(name.c_str(), nullptr, 0);
        if (needed == 0)
        {
            // Not found.
            pinEnvironmentVariableValue = "";
            return;
        }

        std::string res;
        res.resize(static_cast<size_t>(needed - 1));

        GetEnvironmentVariableA(name.c_str(), res.data(), needed);
        pinEnvironmentVariableValue = res;
#else
        const char* v = std::getenv(name.c_str());
        pinEnvironmentVariableValue = v ? v : "";
#endif
	}
};

namespace
{
auto r = Register<EnvironmentVariable>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<PluginList>
    <Plugin id="SE Environment Variable" name="Environment Variable" category="Special">
        <Audio>
            <Pin name="var name" datatype="string"/>
            <Pin name="value" datatype="string" direction="out"/>
        </Audio>
    </Plugin>
</PluginList>
)XML");
}

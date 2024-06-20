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

template <typename T>
class FixedValuesGui final : public SeGuiInvisibleBase
{
	MpGuiPin<T> pinVal;

public:
	FixedValuesGui()
	{
		initializePin(pinVal);
	}
};

namespace
{
	auto r = Register<FixedValuesGui<std::wstring>>::withId(L"SE:GUIFixedValues_text");
	auto r1 = Register<FixedValuesGui<int32_t>>::withId(L"SE:GUIFixedValues_int");
	auto r2 = Register<FixedValuesGui<float>>::withId(L"SE:GUIFixedValues_float");
	auto r3 = Register<FixedValuesGui<bool>>::withId(L"SE:GUIFixedValues_bool");
	auto r4 = Register<FixedValuesGui<int64_t>>::withId(L"SE:GUIFixedValues_int64");
}

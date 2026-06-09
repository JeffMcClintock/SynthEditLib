#include "NativePresetReader.h"
#include <algorithm>
#include <cwctype>
#include "VstPreset.h"
#include "AuPreset.h"

namespace NativePresetUtil
{
	std::string ReadAnyFormat(const std::wstring& filename)
	{
		const auto dot = filename.find_last_of(L'.');
		if (dot == std::wstring::npos)
			return {};

		std::wstring ext = filename.substr(dot + 1);
		std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return std::towlower(c); });

		if (ext == L"vstpreset")
			return VstPresetUtil::ReadPreset(filename);

		if (ext == L"aupreset")
			return AuPresetUtil::ReadPreset(filename);

		return {};
	}
}

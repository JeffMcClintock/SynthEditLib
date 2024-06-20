#pragma once
#include <string>
#include "UUID_Util.h"

// VST3 preset support.

/*
#include "VstPreset.h"
*/

namespace Steinberg
{
	class FUID;
}

namespace VstPresetUtil
{
	void WritePreset(std::wstring filename, std::string categoryName, std::string vendorName, std::string productName, const char* processorId, std::string xmlPreset);

	std::string ReadPreset(std::wstring filename, std::string* returnCategory = nullptr);
}

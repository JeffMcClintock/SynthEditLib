#pragma once
#include <string>

/*
#include "NativePresetReader.h"
*/

// Reads the SynthEdit preset XML payload from a native preset file, choosing the
// decoder by file extension (.vstpreset or .aupreset). This lets the preset
// browser scan BOTH formats regardless of which plugin format is running, so a
// preset saved in the other format is still discovered.
//
// Dependency-light header (std::string only); the VST3-SDK-dependent code lives
// in NativePresetReader.cpp.
namespace NativePresetUtil
{
	// Returns the preset XML, or empty if the extension is unrecognised or the
	// file can't be read.
	std::string ReadAnyFormat(const std::wstring& filename);
}

#pragma once
#include <string>

/*
#include "NativePresetWriter.h"
*/

// Writes a preset in BOTH DAW-native formats (.vstpreset AND .aupreset) side-by-side,
// so a preset saved by the VST3 plugin is discoverable by Audio Unit hosts and vice-versa.
//
// All plugin-identity values are sourced from the shared factory.se.xml (via BundleInfo),
// so the behaviour is identical whether called from the VST3 or the AU build. A format whose
// required identity values are missing from factory.se.xml is skipped (older exports).
//
// This header is intentionally dependency-light (std::string only) so callers don't pull in
// the VST3 SDK; the heavy includes live in NativePresetWriter.cpp.
namespace NativePresetUtil
{
	// 'chosenFilename' is the user-chosen UTF-8 path (any/no extension); its extension is
	// replaced to produce the two sibling files in the same folder with the same base name.
	void WriteAllFormats(
		const std::string& chosenFilename,
		const std::string& presetName,
		const std::string& xml);
}

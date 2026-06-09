#include "NativePresetWriter.h"
#include "VstPreset.h"
#include "AuPreset.h"
#include "conversion.h"
#include "BundleInfo.h"

namespace NativePresetUtil
{
	// Strip the extension (if any) from a UTF-16 path, leaving folder + base name.
	static std::wstring stripExtension(const std::wstring& path)
	{
		const auto dotPos = path.find_last_of(L'.');
		const auto slashPos = path.find_last_of(L"/\\");
		if (dotPos != std::wstring::npos && (slashPos == std::wstring::npos || dotPos > slashPos))
			return path.substr(0, dotPos);
		return path;
	}

	void WriteAllFormats(
		const std::string& chosenFilename,
		const std::string& presetName,
		const std::string& xml)
	{
		const auto& info = BundleInfo::instance()->getPluginInfo();

		// Folder + base name shared by both sibling files (e.g. ".../mypreset").
		const std::wstring basePath = stripExtension(Utf8ToWstring(chosenFilename));

		// --- VST3 (.vstpreset) ---
		// Requires the VST3 component UUID; skip if this build's factory.se.xml predates it.
		if (!info.processorId.empty())
		{
			// Category metadata is left empty here (matching the previous VST3 behaviour); the
			// preset's own category travels inside the xml payload, so it survives either format.
			VstPresetUtil::WritePreset(
				basePath + L".vstpreset",
				std::string{},      // categoryName (MusicalCategory) — see note above
				info.vendorName,
				info.pluginName,
				info.processorId.c_str(),
				xml);
		}

		// --- Audio Unit (.aupreset) ---
		// Requires the AU type code + manufacturer id; skip if missing.
		if (!info.macCategory.empty() && !info.manufacturerId.empty())
		{
			// Convert the 4-char plugin id (stored as a 32-bit int) back to its character form,
			// big-endian / MSB-first — the inverse of conversion.h's idToInt32(), which
			// AuPresetUtil::WritePreset() applies internally.
			const int32_t p_id = info.pluginId;
			char fourCC[5];
			fourCC[0] = static_cast<char>((p_id >> 24) & 0xff);
			fourCC[1] = static_cast<char>((p_id >> 16) & 0xff);
			fourCC[2] = static_cast<char>((p_id >> 8) & 0xff);
			fourCC[3] = static_cast<char>(p_id & 0xff);
			fourCC[4] = 0;

			AuPresetUtil::WritePreset(
				basePath + L".aupreset",
				presetName,
				info.macCategory,
				info.manufacturerId,
				std::string(fourCC),
				xml);
		}
	}
}

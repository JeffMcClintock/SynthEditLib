
#include "GmpiResourceManager.h"
#include "se_filesystem.h"
#include <string>
#include <string_view>
#include "conversion.h"
#include "BundleInfo.h"
#include "ProtectedFile.h"
#include "unicode_conversion.h"

using namespace std;
using namespace JmUnicodeConversions;

namespace fs = se_fs;

bool ResourceExists(const std::wstring& path)
{
	return BundleInfo::instance()->ResourceExists(JmUnicodeConversions::WStringToUtf8(path).c_str());
}

int32_t GmpiResourceManager::FindResourceU(int32_t moduleHandle, const std::string skinName, const char* resourceName, const char* resourceType, gmpi::IString* returnString)
{
	return RegisterResourceUri(moduleHandle, skinName, resourceName, resourceType, returnString, false);
}

int32_t GmpiResourceManager::RegisterResourceUri(int32_t moduleHandle, const std::string skinName, const char* resourceName, const char* resourceType, gmpi::IString* returnString, bool isIMbeddedResource)
{
	gmpi::IString* returnValue = nullptr;
	if (gmpi::MP_OK != returnString->queryInterface(gmpi::MP_IID_RETURNSTRING, reinterpret_cast<void**>(&returnValue)))
		return gmpi::MP_NOSUPPORT;

	const std::wstring resourceNameL = JmUnicodeConversions::Utf8ToWstring(resourceName);
	std::wstring returnUri;

	// Resource-type setup: extension fallback list + which standard folder to search in.
	std::vector<std::wstring> searchExtensions;
	std::wstring standardFolder = resourceFolders[GmpiResourceType::Image];
	bool searchWithSkin = false;

	// Pseudo-files. __fontMetrics/global fall through to the search code (with .txt + skin);
	// __nativePresetsFolder short-circuits with a fixed URI.
	if (isEditor() && (resourceNameL == L"__fontMetrics" || resourceNameL == L"global"))
	{
		searchExtensions.push_back(L".txt");
		searchWithSkin = true;
	}
	else if (!isEditor() && resourceNameL == L"__nativePresetsFolder")
	{
		returnUri = BundleInfo::instance()->getPresetFolder();
	}

	if (returnUri.empty())  // not the short-circuit case
	{
		if (strcmp(resourceType, "ImageMeta") == 0)
		{
			searchExtensions.push_back(L".txt");
			searchWithSkin = true;
		}
		else if (strcmp(resourceType, "Image") == 0 || strcmp(resourceType, "png") == 0 || strcmp(resourceType, "svg") == 0)
		{
			// see also: isSkinImageFile()
			searchExtensions = { L".png", L".bmp", L".jpg", L".svg" };
			searchWithSkin = true;
		}
		else if (strcmp(resourceType, "Audio") == 0 || strcmp(resourceType, "wav") == 0)
		{
			searchExtensions.push_back(L".wav");
			standardFolder = resourceFolders[GmpiResourceType::Audio];
		}
		else if (strcmp(resourceType, "Instrument") == 0 || strcmp(resourceType, "sfz") == 0 || strcmp(resourceType, "sf2") == 0)
		{
			searchExtensions = { L".sf2", L".sfz" };
			standardFolder = resourceFolders[GmpiResourceType::Soundfont];
		}
		else if (strcmp(resourceType, "MIDI") == 0 || strcmp(resourceType, "mid") == 0)
		{
			searchExtensions.push_back(L".mid");
			standardFolder = resourceFolders[GmpiResourceType::Midi];
		}

		// Try `candidate` literally, then with each known extension swapped in. Hits set returnUri.
		// "Hit" means either the file exists on disk or it's been baked into the plugin bundle as
		// an embedded resource (looked up by the URI string itself).
		auto tryPath = [&](const fs::path& candidate) -> bool
		{
			const auto check = [&](const std::wstring& ws) -> bool
			{
				if (FileExists(ws))
				{
					returnUri = ws;
					return true;
				}
				if (ResourceExists(ws))
				{
					returnUri = JmUnicodeConversions::Utf8ToWstring(BundleInfo::resourceTypeScheme) + ws;
					return true;
				}
				return false;
			};
			if (check(candidate.wstring())) return true;
			for (const auto& ext : searchExtensions)
			{
				auto withExt = candidate;
				withExt.replace_extension(ext);
				if (check(withExt.wstring())) return true;
			}
			return false;
		};

		const fs::path name(resourceNameL);

		if (name.is_absolute())
		{
			// Plugin builds may have the resource baked into the bundle under one of two
			// "encoded" forms of its full path. Try both, then fall back to the raw filesystem path.
			if (!isEditor())
			{
				// 1. Every separator → "__", colon → "_". Matches the original-author's PC path
				//    "C:\synth edit projects\scat graphics\duck.png" → "C___synth edit projects__scat graphics__duck.png".
				std::wstring imbeddedName(resourceNameL);
				replacein(imbeddedName, L"/", L"__");
				replacein(imbeddedName, L"\\", L"__");
				replacein(imbeddedName, L":", L"_");
				tryPath(fs::path(standardFolder) / imbeddedName);

				// 2. Skin-shared assets are exported under just their post-"skins/" tail. Strip
				//    everything up to and including the "skins" component so e.g.
				//    "...\skins\PD303\UniqueKnob.png" → "PD303__UniqueKnob.png".
				if (returnUri.empty() && searchWithSkin)
				{
					constexpr std::wstring_view skinsSep1 = L"\\skins\\";
					constexpr std::wstring_view skinsSep2 = L"/skins/";
					auto p = resourceNameL.find(skinsSep1);
					if (p == std::wstring::npos) p = resourceNameL.find(skinsSep2);
					if (p != std::wstring::npos)
					{
						auto tail = resourceNameL.substr(p + skinsSep1.size());
						replacein(tail, L"\\", L"__");
						replacein(tail, L"/", L"__");
						tryPath(fs::path(standardFolder) / tail);
					}
				}
			}

			if (returnUri.empty())
				tryPath(name);
		}
		else
		{
			// Relative-name resolution. searchWithSkin adds a per-skin lookup chain; non-skin
			// types (Audio, Soundfont, MIDI) only check the type's standard folder.
			std::vector<fs::path> searchFolders;
			const fs::path bareName = fs::path(resourceNameL).replace_extension();
			const fs::path stdFolder(standardFolder);

			if (searchWithSkin)
			{
				if (isEditor())
				{
					// Project-specific skin folder first if the project defines one.
					if (auto psf = projectSkinFolder(); !psf.empty())
						searchFolders.push_back(psf / bareName);

					// Then the current skin, default skin, and _fallback (last so user-supplied
					// skin assets win even when the structure view forces a fallback lookup).
					for (const auto& skin : { JmUnicodeConversions::Utf8ToWstring(skinName), std::wstring(L"default"), std::wstring(L"_fallback") })
						searchFolders.push_back(stdFolder / skin / bareName);
				}
				else
				{
					// Plugin export flattens "<skin>/<file>" to "<skin>__<file>" in a single folder.
					// (PSS retained for behaviour parity with the legacy resolver, even though
					// project skins aren't normally exported to plugin bundles.)
					for (const auto& skin : { std::wstring(L"PSS"), JmUnicodeConversions::Utf8ToWstring(skinName), std::wstring(L"default"), std::wstring(L"_fallback") })
						searchFolders.push_back(stdFolder / (skin + L"__" + bareName.wstring()));
				}
			}
			else
			{
				// Project-specific resources folder first if the project defines one (editor only;
				// plugin builds have resources baked in and no access to the original .se1).
				if (isEditor())
				{
					if (auto prf = projectResourcesFolder(); !prf.empty())
						searchFolders.push_back(prf / bareName);
				}
				searchFolders.push_back(stdFolder / bareName);
			}

			for (const auto& candidate : searchFolders)
			{
				if (tryPath(candidate))
					break;
			}
		}
	}

	const std::string fullUri = JmUnicodeConversions::WStringToUtf8(returnUri);

	// NOTE: Had to disable whole-program-optimisation to prevent compiler inlining this(which corrupted std::string). Not sure how it figured out to inline it.
	returnValue->setData(fullUri.data(), static_cast<int32_t>(fullUri.size()));

	if (returnUri.empty())
		return gmpi::MP_FAIL;

	if (isEditor() && isIMbeddedResource)
	{
		assert(moduleHandle > -1);
		resourceUris_.insert({ moduleHandle, fullUri });
	}
	return gmpi::MP_OK;
}

int32_t GmpiResourceManager::RegisterResourceUri(int32_t moduleHandle, const char* fullUri)
{
	if (isEditor())
	{
		assert(moduleHandle > -1);
		resourceUris_.insert({ moduleHandle, fullUri });
	}

	return gmpi::MP_OK;
}

void GmpiResourceManager::ClearResourceUris(int32_t moduleHandle)
{
	resourceUris_.erase(moduleHandle);
}

void GmpiResourceManager::ClearAllResourceUris()
{
	resourceUris_.clear();
}

int32_t GmpiResourceManager::OpenUri(const char* fullUri, gmpi::IProtectedFile2** returnStream)
{
	std::string uriString(fullUri);
	if (uriString.find(BundleInfo::resourceTypeScheme) == 0)
	{
		*returnStream = new ProtectedMemFile2(BundleInfo::instance()->getResource(fullUri + strlen(BundleInfo::resourceTypeScheme)));
	}
	else
	{
		*returnStream = ProtectedFile2::FromUri(fullUri);
	}

	return *returnStream != nullptr ? (gmpi::MP_OK) : (gmpi::MP_FAIL);
}

se_fs::path GmpiResourceManager::ResolveResourceUri(const se_fs::path& filename, std::wstring_view skinName)
{
	namespace fs = se_fs;

	// Already absolute - return as-is
	if (filename.is_absolute())
		return filename;

	// Build search folders list: projectSkinFolder, then skins/skinName, skins/default, skins/_fallback
	std::vector<fs::path> searchFolders;

	if (const auto psf = projectSkinFolder(); !psf.empty())
		searchFolders.push_back(psf);

	const fs::path skinsFolder(resourceFolders[GmpiResourceType::Image]);
	searchFolders.push_back(skinsFolder / skinName);
	searchFolders.push_back(skinsFolder / L"default");
	searchFolders.push_back(skinsFolder / L"_fallback");

	for (const auto& folder : searchFolders)
	{
		auto candidate = folder / filename;
		if (fs::exists(candidate))
			return candidate;
	}

	// Fallback to skins/default if nothing found
	return skinsFolder / L"default" / filename;
}

std::string GmpiResourceManager::ShortenResourceUri(const std::string& fullPath)
{
	namespace fs = se_fs;

	const fs::path path(fullPath);
	const auto filename = path.filename();

	if (isEditor())
	{
		// Check project-specific skin folder first (e.g., mysynth.skin/)
		if (const auto psf = projectSkinFolder(); !psf.empty())
		{
			if (fullPath.starts_with(psf.string()))
			{
				// File is in project skin folder - return just the filename
				return filename.string();
			}
		}

		// Check standard skins folder
		const auto& standardFolder = resourceFolders[GmpiResourceType::Image];
		if (!standardFolder.empty())
		{
			const fs::path skinsFolder(standardFolder);
			const auto relativePath = fs::relative(path, skinsFolder);

			// If it's within the skins folder and relative path doesn't start with ".."
			if (!relativePath.empty() && relativePath.begin()->string() != "..")
			{
				// Skip the skin name component (e.g., "Blue/knob.png" -> "knob.png")
				auto it = relativePath.begin();
				if (it != relativePath.end())
					++it; // skip skin folder name

				fs::path result;
				for (; it != relativePath.end(); ++it)
					result /= *it;

				return result.string();
			}
		}
	}

	// Return original path if not in any known folder
	return fullPath;
}


#include "GmpiResourceManager.h"
#include <string>
//#include <regex> 
#include "conversion.h"
#include "BundleInfo.h"
#include "ProtectedFile.h"
//#include "mfc_emulation.h"
#include "unicode_conversion.h"

using namespace std;
using namespace JmUnicodeConversions;

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
	gmpi::IString* returnValue = 0;

	if (gmpi::MP_OK != returnString->queryInterface(gmpi::MP_IID_RETURNSTRING, reinterpret_cast<void**>(&returnValue)))
	{
		return gmpi::MP_NOSUPPORT;
	}

	wstring uri;
	wstring returnUri;
	wstring resourceNameL = JmUnicodeConversions::Utf8ToWstring(resourceName);

	vector<wstring> searchExtensions;
	bool searchWithSkin = false;
	auto bare = StripExtension(resourceNameL);

	wstring standardFolder = resourceFolders[GmpiResourceType::Image];

	if (isEditor())
	{
		if (resourceNameL == L"__fontMetrics" || resourceNameL == L"global") // special magic psudo file.
		{
			returnUri = combine_path_and_file(JmUnicodeConversions::Utf8ToWstring(skinName), JmUnicodeConversions::Utf8ToWstring(resourceName)) + L".txt";
			goto storeFullUri;
		}
	}
	else
	{
		if (resourceNameL == L"__nativePresetsFolder") // special magic 'folder'.
		{
			returnUri = BundleInfo::instance()->getPresetFolder();
			goto storeFullUri;
		}

//		standardFolder = BundleInfo::instance()->getResourceFolder();
	}

	if (strcmp(resourceType, "ImageMeta") == 0)
	{
		searchExtensions.push_back(L".txt");
		searchWithSkin = true;
	}
	else
	{
		if (strcmp(resourceType, "Image") == 0 || strcmp(resourceType, "png") == 0 || strcmp(resourceType, "svg") == 0)
		{
			// see also: isSkinImageFile()
			searchExtensions.push_back(L".png");
			searchExtensions.push_back(L".bmp");
			searchExtensions.push_back(L".jpg");
			searchExtensions.push_back(L".svg");
			searchWithSkin = true;
		}
		else
		{
			if (strcmp(resourceType, "Audio") == 0 || strcmp(resourceType, "wav") == 0)
			{
				searchExtensions.push_back(L".wav");
				standardFolder = resourceFolders[GmpiResourceType::Audio];
			}
			else
			{
				if (strcmp(resourceType, "Instrument") == 0 || strcmp(resourceType, "sfz") == 0 || strcmp(resourceType, "sf2") == 0)
				{
					searchExtensions.push_back(L".sf2");
					searchExtensions.push_back(L".sfz");
					standardFolder = resourceFolders[GmpiResourceType::Soundfont];
				}
				else
				{
					if (strcmp(resourceType, "MIDI") == 0 || strcmp(resourceType, "mid") == 0)
					{
						searchExtensions.push_back(L".mid");
						standardFolder = resourceFolders[GmpiResourceType::Midi];
					}
				}
			}
		}
	}


	// Full filenames.
	if (resourceNameL.find(L':') != string::npos)
	{
		std::vector<wstring> searchPaths;

		if (!isEditor())
		{
			// Cope with "encoded" full filenames. e.g. "C___synth edit projects__scat graphics__duck.png" ( was "C:\\synth edit projects\scat graphics\duck.png")
			std::wstring imbeddedName(resourceNameL);
			replacein(imbeddedName, (L"/"), (L"__"));
			replacein(imbeddedName, (L"\\"), (L"__")); // single backslash (escaped twice).
			replacein(imbeddedName, (L":"), (L"_"));

			searchPaths.push_back(combine_path_and_file(standardFolder, imbeddedName));

			// Paths to non-default, non-standard skin files.  e.g. skins/PD303/UniqueKnob.png (where same image is NOT in default folder).
			// These are stored as full paths by modules to prevent fallback to default skin. However export routine stores these with short names "PD303__UniqueKnob.png".
			if (searchWithSkin)
			{
				auto p = resourceNameL.find(L"\\skins\\");
				if (p != string::npos)
				{
					auto imbeddedName2 = resourceNameL.substr(p + 7);
					// imbeddedName2 = std::regex_replace(imbeddedName2, std::basic_regex<wchar_t>(L"\\\\"), L"__"); // single backslash (escaped twice).
					replacein(imbeddedName2, (L"\\"), (L"__")); // single backslash (escaped twice).
					searchPaths.push_back(combine_path_and_file(standardFolder, imbeddedName2)); // prepend resource folder.
				}
			}
		}

		searchPaths.push_back(resourceNameL); // literal long filename. e.g. "C:\Program Files\Whatever\knob.bmp", "C:\Program Files\Whatever\knob" (.txt)

		auto originalExtension = GetExtension(resourceNameL);

		assert(returnUri.empty());

		for (auto path : searchPaths)
		{
			if (returnUri.empty())
			{
				// If extension provided, search that first.
				if (!originalExtension.empty())
				{
					if (FileExists(path))
					{
						returnUri = path;
						break;
					}

					if (ResourceExists(path))
					{
						returnUri = JmUnicodeConversions::Utf8ToWstring(BundleInfo::resourceTypeScheme) + path;
						break;
					}
				}

				// Search different extensions. png, bpm, jpg.
				auto barePath = StripExtension(path);
				for (auto ext : searchExtensions)
				{
					auto temp = barePath + ext;
					if (FileExists(temp))
					{
						returnUri = temp;
						break;
					}
					if (ResourceExists(temp))
					{
						returnUri = JmUnicodeConversions::Utf8ToWstring(BundleInfo::resourceTypeScheme) + temp;
						break;
					}
				}
			}
		}
	}
	else
	{
		// partial filenames (no drive or root slash)
		// Build list of search folders and filename templates
		std::vector<std::pair<std::wstring, std::wstring>> searchFolders; // {folder, filenameTemplate}

		if (searchWithSkin)
		{
			const std::vector<std::wstring> skinNames = {
				L"PSS",	// project-specific-skin
				JmUnicodeConversions::Utf8ToWstring(skinName),
				L"default",
				L"_fallback" // fallback is last location searched. It is to avoid the problem of the structure-view not finding resources unless they are put in default skin (which is not meant for user content)
			};

			if (isEditor())
			{
				for (const auto& skin : skinNames)
				{
					if(L"PSS" == skin)
					{
						// Search project-specific skin folder first (e.g., mysynth.skin/)
						const auto psf = projectSkinFolder();
							if (!psf.empty())
							{
								const auto psfStr = psf.wstring();
								searchFolders.push_back({ psfStr, combine_path_and_file(psfStr, bare) });
							}
					}
					else
					{
						auto filenameTemplate = combine_path_and_file(standardFolder, combine_path_and_file(skin, bare));
						searchFolders.push_back({ standardFolder, filenameTemplate });
					}
				}
			}
			else
			{
				// VST3/MyPlugin/blue__filename
				for (const auto& skin : skinNames)
				{
					auto filenameTemplate = combine_path_and_file(standardFolder, skin + L"__" + bare);
					searchFolders.push_back({ standardFolder, filenameTemplate });
				}
			}
		}
		else
		{
			searchFolders.push_back({ standardFolder, combine_path_and_file(standardFolder, bare) });
		}

		for (const auto& [folder, uri] : searchFolders)
		{
			// First try given extension (if bare filename already has one).
			if (FileExists(uri))
			{
				returnUri = uri;
				break;
			}
			if (ResourceExists(uri))
			{
				returnUri = JmUnicodeConversions::Utf8ToWstring(BundleInfo::resourceTypeScheme) + uri;
				break;
			}

			// Search different extensions. png, bmp, jpg, etc.
			for (const auto& ext : searchExtensions)
			{
				auto temp = uri + ext;
				if (FileExists(temp))
				{
					returnUri = temp;
					break;
				}
				if (ResourceExists(temp))
				{
					returnUri = JmUnicodeConversions::Utf8ToWstring(BundleInfo::resourceTypeScheme) + temp;
					break;
				}
			}

			if (!returnUri.empty())
				break;
		}
	}

	storeFullUri:

	string fullUri = JmUnicodeConversions::WStringToUtf8(returnUri);

	// NOTE: Had to disable whole-program-optimisation to prevent compiler inlining this(which corrupted std::string). Not sure how it figured out to inline it.
	returnValue->setData(fullUri.data(), (int32_t) fullUri.size());

	if( returnUri.empty() )
	{
#ifdef _DEBUG
//		_RPT1(0, "GmpiResourceManager::RegisterResourceUri(%s) Not Found\n", resourceName);
#endif
		return gmpi::MP_FAIL;
	}

	if(isEditor() && isIMbeddedResource)
	{
		// Cache name.
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

std::filesystem::path GmpiResourceManager::ResolveResourceUri(const std::filesystem::path& filename, std::wstring_view skinName)
{
	namespace fs = std::filesystem;

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
	namespace fs = std::filesystem;

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

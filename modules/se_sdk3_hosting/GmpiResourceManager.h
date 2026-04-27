#pragma once
#include <filesystem>
#include <map>
#include <unordered_map>
#include "mp_sdk_common.h"

enum class GmpiResourceType
{
	Image,
	Audio,
	Soundfont,
	Midi
};

class GmpiResourceManager
{
public:
	static GmpiResourceManager* Instance(); // ref: platform_plugin.cpp or GmpiResourceManager_editor.cpp

	std::filesystem::path projectFile; // path up to first period. e.g. "C:\mydocument.version23.synthedit" => "C:\mydocument"

	// Returns the project-specific skin folder (e.g. "C:\mysynth.skin"), or empty if no project is set.
	std::filesystem::path projectSkinFolder() const
	{
		if (projectFile.empty())
			return {};
		return projectFile.parent_path() / (projectFile.stem().wstring() + L".skin");
	}

	void setProjectFile(const std::filesystem::path& fullPath)
	{
		if (fullPath.empty())
		{
			projectFile.clear();
			return;
		}

		const auto parent = fullPath.parent_path();
		const auto filename = fullPath.filename().wstring();
		const auto dotPos = filename.find(L'.');
		if (dotPos != std::wstring::npos)
			projectFile = parent / filename.substr(0, dotPos);
		else
			projectFile = fullPath;
	}

	std::multimap< int32_t, std::string > resourceUris_;
	std::unordered_map< GmpiResourceType, std::wstring > resourceFolders;
	virtual bool isEditor() { return false; }

	void ClearResourceUris(int32_t moduleHandle);
	void ClearAllResourceUris();
	int32_t RegisterResourceUri(int32_t moduleHandle, const std::string skinName, const char* resourceName, const char* resourceType, gmpi::IString* returnString, bool isIMbeddedResource = true);
	int32_t RegisterResourceUri(int32_t moduleHandle, const char* fullUri);
	int32_t FindResourceU(int32_t moduleHandle, const std::string skinName, const char* resourceName, const char* resourceType, gmpi::IString* returnString);
	std::string ShortenResourceUri(const std::string& fullPath);
	std::filesystem::path ResolveResourceUri(const std::filesystem::path& filename, std::wstring_view skinName);
	virtual int32_t OpenUri(const char* fullUri, gmpi::IProtectedFile2** returnStream);
};


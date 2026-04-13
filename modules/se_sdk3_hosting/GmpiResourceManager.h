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

	std::filesystem::path projectFile; // mysynth.synthedit has implied mysynth.skin skin folder.

	std::multimap< int32_t, std::string > resourceUris_;
	std::unordered_map< GmpiResourceType, std::wstring > resourceFolders;
	virtual bool isEditor() { return false; }

	void ClearResourceUris(int32_t moduleHandle);
	void ClearAllResourceUris();
	int32_t RegisterResourceUri(int32_t moduleHandle, const std::string skinName, const char* resourceName, const char* resourceType, gmpi::IString* returnString, bool isIMbeddedResource = true);
	int32_t RegisterResourceUri(int32_t moduleHandle, const char* fullUri);
	int32_t FindResourceU(int32_t moduleHandle, const std::string skinName, const char* resourceName, const char* resourceType, gmpi::IString* returnString);
	virtual int32_t OpenUri(const char* fullUri, gmpi::IProtectedFile2** returnStream);
};


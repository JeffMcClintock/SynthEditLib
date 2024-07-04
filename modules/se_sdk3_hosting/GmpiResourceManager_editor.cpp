
#include "GmpiResourceManager_editor.h"
#include "conversion.h"
#include "ProtectedFile.h"
#include "SkinMgr.h"

using namespace std;

// Meyer's singleton. see also platform_plugin.cpp
GmpiResourceManager* GmpiResourceManager::Instance()
{
	static GmpiResourceManager_editor obj;
	return &obj;
}

int32_t GmpiResourceManager_editor::OpenUri(const char* fullUri, gmpi::IProtectedFile2** returnStream)
{
	if (auto sp = strstr(fullUri, "__fontMetrics"); sp) // special magic 'file'.
	{
		std::string skinName(fullUri, sp - fullUri - 1);
		std::string temp = SkinMgr::Instance()->getSkin(Utf8ToWstring(skinName))->GetPixelHeights();
		*returnStream = new ProtectedMemFile2(temp.data(), temp.size());
		return gmpi::MP_OK;
	}

	
	if (auto sp = strstr(fullUri, "global.txt");sp) // special magic 'file'.
	{
		std::string skinName(fullUri, sp - fullUri - 1);
		std::string temp = SkinMgr::Instance()->getEffectiveFontInfo(Utf8ToWstring(skinName).c_str());
		*returnStream = new ProtectedMemFile2(temp.data(), temp.size());
		return gmpi::MP_OK;
	}

	return GmpiResourceManager::OpenUri(fullUri, returnStream);
}

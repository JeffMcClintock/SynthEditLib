#ifdef _WIN32
#undef  WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#undef  NOMINMAX
#define NOMINMAX
#include <Windows.h>
#include <Shlobj.h>
#include "legacyExternalApp.h"

#include "conversion.h"

std::unique_ptr<legacyExternalApp> legacyExternalApp::create()
{
	TCHAR path[MAX_PATH];
	SHGetFolderPath(nullptr, CSIDL_PROGRAM_FILES, nullptr, SHGFP_TYPE_CURRENT, path); // windows-only

	const std::wstring synthEditPath = std::wstring(path) + L"\\SynthEdit 1.5\\SynthEdit.exe";

	if(!FileExists(synthEditPath))
		return {};

	int legacyBuildNumber = 0;

	// check version
	DWORD  verHandle = 0;
	UINT   size = 0;
	LPBYTE lpBuffer = nullptr;
	DWORD  verSize = GetFileVersionInfoSize(synthEditPath.c_str(), &verHandle);

	if (verSize != NULL)
	{
		LPSTR verData = new char[verSize];

		if (GetFileVersionInfo(synthEditPath.c_str(), verHandle, verSize, verData))
		{
			if (VerQueryValue(verData, L"\\", (VOID FAR * FAR*) & lpBuffer, &size))
			{
				if (size)
				{
					VS_FIXEDFILEINFO* verInfo = (VS_FIXEDFILEINFO*)lpBuffer;
					if (verInfo->dwSignature == 0xfeef04bd)
					{
						legacyBuildNumber = (verInfo->dwFileVersionLS >> 16) & 0xffff;
					}
				}
			}
		}
		delete[] verData;
	}

	if (legacyBuildNumber < 643) // This build introduced '/upgradexml' command line arg
		return {};

	return std::make_unique<legacyExternalApp>(synthEditPath);
}

bool legacyExternalApp::UpgradeProjectFile(std::wstring projectFilepath)
{
	const std::wstring command = synthEditPath + L" \"" + projectFilepath + L"\" /upgradexml /quiet";

	STARTUPINFOW sui = {};
	sui.wShowWindow = SW_SHOWMINIMIZED;

	PROCESS_INFORMATION pi;

	CreateProcessW(
		nullptr,
		(LPWSTR)command.c_str(),
		nullptr,
		nullptr,
		FALSE,
		0,
		0, 0, &sui, &pi);

	WaitForSingleObject(pi.hProcess, INFINITE);

	//close handles opened by CreateProcess
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	return true;
}
#endif

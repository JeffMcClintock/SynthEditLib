#pragma once
#include <memory>
#include <string>

class legacyExternalApp
{
	std::wstring synthEditPath;

public:
	static std::unique_ptr<legacyExternalApp> create();

	legacyExternalApp(std::wstring psynthEditPath) : synthEditPath(psynthEditPath) {}
	bool UpgradeProjectFile(std::wstring projectFilepath);
};
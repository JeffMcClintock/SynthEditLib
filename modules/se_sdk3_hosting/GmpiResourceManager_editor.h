#pragma once
#include "GmpiResourceManager.h"

class GmpiResourceManager_editor : public GmpiResourceManager
{
public:
	bool isEditor() override { return true; }
	int32_t OpenUri(const char* fullUri, gmpi::IProtectedFile2** returnStream) override;
};


#pragma once
#include "CUG.h"

class CPlugin : public CUG
{
public:
	CPlugin( Module_Info* p_type = 0 );
	~CPlugin();
	DECLARE_BUILD_FUNC(CPlugin);
	virtual void ReloadDll();
	void OnHelp();
	void Upgrade(int from_version);
private:
	bool DllAvailable();
};

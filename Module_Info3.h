#pragma once

#include "Module_Info3_base.h"

#ifndef _WIN32
#include <dlfcn.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "./modules/shared/xp_dynamic_linking.h"
#include "SerializationHelper_XML.h"

class Module_Info3 : public Module_Info3_base
{
public:
	Module_Info3( const std::wstring& file_and_dir, const std::wstring& overridingCategory = L"" );
	~Module_Info3();

	bool OnDemandLoad() override
	{
		return !LoadDllOnDemand();
	}
	bool isSummary() override // for VST2 plugins held as name-only.
	{
		return m_parameters.empty() && gui_plugs.empty() && plugs.empty(); // reasonable guess.
	}

	void LoadDll();
	void ReLoadDll();
	void Unload();
	std::wstring Filename() override
	{
		return filename;
	}

	gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> getFactory2();

	ug_base* BuildSynthOb() override;
	gmpi::IMpUnknown* Build(int subType, bool quietFail) override;

	bool FileInUse() override
	{
    #if defined(_WIN32)
		return dllHandle != 0;
        #else
    assert(false);
        return false;
    #endif
	}
	bool fromExternalDll() override { return true;};

	bool LoadDllOnDemand();
	int getClassType() override { return 0; } // 0 - Module_Info3, 1 - Module_Info, 2 - Module_Info3_internal, 3 - Module_Info_Plugin
	
	std::pair< gmpi_dynamic_linking::DLL_HANDLE, std::wstring > getDllInfo()
	{
		return std::make_pair(dllHandle, filename);
	}

	std::wstring filename;

protected:
	Module_Info3(); // serialisation only

	gmpi_dynamic_linking::DLL_HANDLE dllHandle;
};

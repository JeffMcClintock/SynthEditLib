#pragma once

#include <filesystem>
#include "Module_Info3_base.h"

#ifndef _WIN32
#include <dlfcn.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

#include "./modules/shared/xp_dynamic_linking.h"
#include "SerializationHelper_XML.h"

class PluginHolder
{
	gmpi_dynamic_linking::DLL_HANDLE dllHandle = {};
	std::filesystem::path pluginPath; // actual dll on Windows (possible inside bundle), enclosing Bundle folder on macOS (not the binary)

public:
	PluginHolder() = default;
	PluginHolder(std::filesystem::path binary_path)
	{
		setPluginPath(binary_path);
	}
	~PluginHolder();
	
	bool isLoaded() const
	{
		return dllHandle != 0;
	}
	std::filesystem::path getPluginPath() const
	{
		return pluginPath;
	}
	void setPluginPath(std::filesystem::path pPath)
	{
		pluginPath = pPath;
	}
	void load();
	void unload();

	gmpi_dynamic_linking::DLL_HANDLE getHandle() const
	{
		return dllHandle;
	}
	gmpi::MP_DllEntry getFactory();
};

class Module_Info3 : public Module_Info3_base
{
public:
	Module_Info3( const std::wstring& file_and_dir, const std::wstring& overridingCategory = L"" );

	void ScanXml(tinyxml2::XMLElement* xmlData) override;

	bool OnDemandLoad() override
	{
		return LoadDllOnDemand();
	}
	bool isSummary() override // for VST2 plugins held as name-only.
	{
		return m_parameters.empty() && gui_plugs.empty() && plugs.empty(); // reasonable guess.
	}

	void ReLoadDll();
	std::wstring Filename() override
	{
		return holder.getPluginPath().wstring();
	}

	gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> getFactory2();

	ug_base* BuildSynthOb() override;
	gmpi::IMpUnknown* Build(int subType, bool quietFail) override;

	bool fromExternalDll() override { return true;};

	bool LoadDllOnDemand();
	int getClassType() override { return 0; } // 0 - Module_Info3, 1 - Module_Info, 2 - Module_Info3_internal, 3 - Module_Info_Plugin
    int ModuleTechnology() override;

	/* TODO
	std::pair< gmpi_dynamic_linking::DLL_HANDLE, std::wstring > getDllInfo()
	{
		return std::make_pair(dllHandle, filename);
	}
	*/

#ifdef _WIN32
	std::wstring macSemBundlePath; // path to mac SEM on windows-only, where it can be a different file.
#endif
	// path to the plugin for the current platform (win or mac).
	PluginHolder holder;

protected:
	Module_Info3(); // serialisation only
};

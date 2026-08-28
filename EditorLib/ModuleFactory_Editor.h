#pragma once
#include <filesystem>
#include "UgDatabase.h"

// the information needed to present the list of modules to the user.
struct menuinfo
{
	std::wstring name;
	std::wstring group;
	std::wstring unique_id;
	int flavor = 0;
};

// functions related to the Module database, but not needed/relevant in plugins.
extern std::unordered_map< std::wstring, std::unique_ptr<Module_Info> > m_in_use_old_module_list; // used during file loading to hold tempory module info

CDocOb* CreateDocObject(std::wstring p_module_id);
CDocOb* CreateDocObject(Module_Info* p_module_info);

bool isCompatibleWith(Module_Info* ths, Module_Info* other);

void SetAsidePluginData(std::filesystem::path);
void SetAsideAllPluginData(bool shellPlugins = false);
void DeleteTemporaryModuleDescriptions(void);
void RetainMissingModuleDescriptions(void);

void ScanFolder(const std::filesystem::path& p_path, const std::string& p_extension, const std::wstring& sub_menu = {}, bool scanShellPlugins = false);
#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- the binary loader; ScanFolder above stays
void ScanBundle(const std::wstring& group_name, const std::filesystem::path& bundle_path, bool scanShellPlugins = false);
void ScanFile(const std::wstring& group_name, const std::filesystem::path& binary_path);
#endif // SE_NO_EXTERNAL_MODULES

void ExportModuleData(tinyxml2::XMLElement* doc, ExportFormatType format);
#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- SEM cache + dll load/unload
void StoreModuleData();
bool LoadModuleData();

std::wstring UnloadDll(std::wstring dllShortName);
void ReloadDll(std::filesystem::path dllPath);

bool ClearModuleDataCache();
#endif // SE_NO_EXTERNAL_MODULES
void ImportModuleInfo(tinyxml2::XMLElement* documentE, ExportFormatType targetType, int fileFormatVersion);
void RegisterExternalPluginsXml(
	  tinyxml2::XMLDocument* doc
	, const std::wstring& full_path
	, const std::filesystem::path& full_mac_binary_path
	, const std::wstring& group_name
	, bool isShellPlugin = false
);

std::wstring GetName(Module_Info*);
std::wstring GetGroupName(Module_Info*);

void SaveModuleInfoPinXml(InterfaceObject* pin, ExportFormatType format, class TiXmlElement* DspXml, int& expectedId);
void SaveModuleInfoPinXml(InterfaceObject* pin, ExportFormatType format, tinyxml2::XMLElement* DspXml, int& expectedId);
void ExportModuleInfo(tinyxml2::XMLNode* documentE, ExportFormatType fileType);
std::multimap<std::wstring, menuinfo> ExportModuleNames();
tinyxml2::XMLElement* ExportModuleInfo(Module_Info* info, tinyxml2::XMLNode* element, ExportFormatType format, const std::string& overrideModuleId = "", const std::string& overrideModuleName = "");
//void Import(Module_Info* info, tinyxml2::XMLElement* pluginE, ExportFormatType format);
void FlagHostControls(Module_Info* info, bool* flaggedHostControls);
Module_Info* GetByIdSerializing(const std::wstring& p_id);

#ifdef _WIN32
#include "ShlObj.h"
#endif

#include <sstream>
#include <vector>
#include <filesystem>
#if !defined( _WIN32 )
#include <dlfcn.h>
#endif
#include "ModuleFactory_Editor.h"
#include "UgDatabase.h"
#include "SynthEditDocBase.h"
#include "Module_Info3.h"
#include "Module_Info3_internal.h"
#include "UG2.h"
#include "se_file_format_version.h"
#include "modules/shared/xp_dynamic_linking.h"
#include "tinyxml/tinyxml.h"
#include "SafeMessageBox.h"
#include "XmlErrorReport.h"
#include "midi_defs.h"

#include "GmpiApiCommon.h"
#include "BundleInfo.h"

// legacy DOCObs
#include "Ctl_Combo.h"
#include "Ctl_Text.h"
#include "Ctl_Slider.h"
#include "Ctl_Keyboard2.h"
#include "CContainer.h"
#include "mfc_emulation.h"
#include "se_version.h"

using namespace gmpi_sdk;

// used during file loading to hold tempory module info
std::unordered_map< std::wstring, std::unique_ptr<Module_Info> > m_in_use_old_module_list;

const std::unordered_map<int, const wchar_t*> legacyModuleGroups =
{
{32829, L"Debug"},
{150, L"Diagnostic"},
{301, L"Filters"},
{144, L"Old"},
{325, L"Logic"},
{312, L"Math"},
{317, L"Effects"},
{303, L"Modifiers"},
{292, L"MIDI"},
{32828, L"Controls"}, // IDS_MG_CONTROLS
{299, L"Conversion"},
{32820, L"Waveform"},
{323, L"Input-Output"},
{239, L"Flow Control"},
{131, L"Old/Sub-Controls"},
{324, L"Special"},
};

const std::unordered_map<int, const wchar_t*> legacyModuleNames =
{
{480, L"1 kHz Tone"},
{501, L"1 Pole HP"},
{458, L"1 Pole LP"},
{450, L"8 Stage EG"},
{402, L"ADSR"},
{452, L"AND Gate"},
{467, L"All Pass"},
{482, L"Binary Counter"},
{512, L"Ceil"},
{419, L"Clipper"},
{498, L"Comparator"},
{403, L"Container"},
{431, L"Controllers"},
{483, L"Decade Counter"},
{417, L"Delay"},
{517, L"Delay2"},
{504, L"Denormal Cleaner"},
{505, L"Denormal Detector"},
{507, L"Divide"},
{462, L"Fixed Values (Volts)"},
{155, L"Adder"},
{141, L"Float to Volts"},
{511, L"Floor"},
{404, L"IO Mod"},
{464, L"Keyboard"},
{411, L"Level Adj"},
{32829, L"Debug"},
{407, L"List Entry"},
{490, L"MIDI Automator"},
{491, L"MIDI Automator Out"},
{492, L"MIDI Filter"},
{430, L"MIDI Out"},
{409, L"MIDI to CV"},
{489, L"Monostable"},
{448, L"Moog Filter"},
{506, L"Multiply"},
{455, L"NAND Gate"},
{357, L"NOR Gate"},
{456, L"NOT Gate"},
{453, L"OR Gate"},
{510, L"OS Command"},
{32819, L"Oscillator"},
{437, L"Pan"},
{441, L"Peak Follower"},
{32822, L"Phase Dist Osc"},
{503, L"Quantizer"},
{451, L"Rectifier"},
{461, L"Ring Modulator"},
{151, L"Default Setter"},
{135, L"Int To List"},
{474, L"Unnamed (not on menu)"},
{415, L"Patch Automator"},
{146, L"PP Setter"},
{153, L"SynthEdit Patch Parameter Watcher"},
{412, L"SV Filter"},
{465, L"Sample And Hold"},
{201, L"List Converter A"},
{485, L"Shift Register"},
{405, L"Slider"},
{516, L"Sound In"},
{515, L"Sound Out"},
{493, L"Step Counter (old?)"},
{525, L"Step Counter2"},
{508, L"Subtract"},
{438, L"Switch (1->Many)"},
{439, L"Switch (Many->1)"},
{134, L"System Command2"},
{406, L"Text Entry"},
{484, L"Trigger To MIDI"},
{413, L"VCA"},
{440, L"VST Plugin"},
{518, L"Voice Combiner"},
{152, L"Voice Splitter"},
{499, L"Volts To List"},
{487, L"Volts To List2"},
{142, L"Volts to Float"},
{156, L"Volts to Float2"},
{522, L"Wave Player"},
{523, L"Wave Recorder"},
{442, L"X-Mix"},
{454, L"XOR Gate"},
{148, L"Random Voltage"},
{494, L"VST Input"},
{495, L"VST Output"},
};

std::wstring GetGroupName(Module_Info* info)
{
	if (info->sid_group)
	{
		return legacyModuleGroups.find(info->sid_group)->second;
	}

	return info->m_group_name;
}

std::wstring GetName(Module_Info* info)
{
	if (info->sid_name)
	{
		return legacyModuleNames.find(info->sid_name)->second;
	}

	return info->m_name;
}

// provide extensibility to add extra modules on a per-project basis.
// SE2JUCE Controller must implement this function
extern void initialise_synthedit_extra_modules(bool passFalse)
{
	// here to satisfy linker
}

#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- SEM cache
std::wstring SemCacheName()
{
	std::wostringstream oss;
	oss << L"Plugin-Cache-" << EXE_VERSION_NUM / 10000;

	// If the factory folder was set by USER intent (test harness, SynthEditCL's
	// -factorysemsfolder, etc.) rather than the app's own default, the user's
	// installed-app cache must not be clobbered by this run — and repeat runs
	// against the same override folder should share one cache (so a 20-test
	// sweep scans once, not 20 times). Hash the override path into a per-folder
	// suffix; ClearModuleDataCache() routes through SemCacheName() too, so
	// `-rescan -factorysemsfolder X` deletes/rebuilds the right file.
	const auto& bi = *BundleInfo::instance();
	if (bi.isSemFolderOverridden)
	{
		const auto hash = std::hash<std::wstring>{}(bi.semFolder);
		oss << L"-override-" << std::hex << hash;
	}

	oss << L".xml";

	return oss.str();
}
#endif // SE_NO_EXTERNAL_MODULES

// construct object
CDocOb* CreateDocObject(std::wstring p_module_id)
{
	auto mi = ModuleFactory()->GetById(p_module_id);

	if (!mi) // preventing load of file with missing modules. || !mi->OnDemandLoad() )
	{
		return nullptr;
	}

	return CreateDocObject(mi);
}

CDocOb* CreateDocObject(Module_Info* p_module_info)
{
	if (auto mi3b = dynamic_cast<Module_Info3_base*>(p_module_info) ; mi3b)
	{
		if (auto mi3 = dynamic_cast<Module_Info3*>(p_module_info); mi3)
		{
#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b
			// Shell plugs scan only the name. Load and do a proper scan.
			if (mi3->isShellPlugin())
			{
				mi3->LoadDllOnDemand(); // perform a full scan.
			}
#endif // SE_NO_EXTERNAL_MODULES
		}

		return new CUG2(p_module_info);
	}

	// to remove lib dependance on Ctl_Combo etc, register their GUI objects here.
	const auto module_id = p_module_info->UniqueId();

	if (L"Container" == module_id)
		return CContainer::Make(p_module_info);

	if (L"List Entry" == module_id)
		return Ctl_Combo::Make(p_module_info);

	if (L"KeyBoard" == module_id)
		return Ctl_Keyboard2::Make(p_module_info);

	if (L"Slider" == module_id)
		return Ctl_Slider::Make(p_module_info);

	if (L"Text Entry" == module_id)
		return Ctl_Text::Make(p_module_info);

	assert(L"Line" != module_id);

	return CUG::Make(p_module_info);
}

// Check stored pin data compatible with local version of module (rough check).
// Could also check datatypes.
bool isCompatibleWith(Module_Info* ths, Module_Info* other)
{
	if (ths->GuiPlugCount() != other->GuiPlugCount() || ths->PlugCount() != other->PlugCount() || ths->m_parameters.size() != other->m_parameters.size())
		return false;

	// Compare GUI pins.
	{
		auto it3 = other->gui_plugs.begin();
		for (auto it4 = ths->gui_plugs.begin(); it4 != ths->gui_plugs.end() && it3 != other->gui_plugs.end(); ++it4)
		{
			auto p1 = (*it4).second;
			auto p2 = (*it3).second;
			if (p1->getPlugDescID() != p2->getPlugDescID())
			{
				return false;
			}
			const auto defaultA = uniformDefaultString(p1->GetDefaultVal(), p1->GetDatatype());
			const auto defaultB = uniformDefaultString(p2->GetDefaultVal(), p2->GetDatatype());
			if (defaultA != defaultB)
			{
				return false;
			}

			++it3;
		}
	}
	// compare DSP pins
	{
		auto it3 = other->plugs.begin();
		for (auto it4 = ths->plugs.begin(); it4 != ths->plugs.end() && it3 != other->plugs.end(); ++it4)
		{
			auto p1 = (*it4).second;
			auto p2 = (*it3).second;
			if (p1->getPlugDescID() != p2->getPlugDescID())
			{
				return false;
			}

			// compare default value, because it will silenty change any defaulted pin value.
			if (DT_MIDI2 != p1->GetDatatype())
			{
				const auto defaultA = uniformDefaultString(p1->GetDefaultVal(), p1->GetDatatype());
				const auto defaultB = uniformDefaultString(p2->GetDefaultVal(), p2->GetDatatype());
				if (defaultA != defaultB && (p1->GetDirection() == DR_IN || p1->isSettableOutput()) )
				{
					return false;
				}
			}

			++it3;
		}
	}
	// Compare Parameters.
	auto it2 = other->m_parameters.begin();
	for (auto it = ths->m_parameters.begin(); it != ths->m_parameters.end(); ++it)
	{
		if ((*it).first != (*it2).first)
		{
			return false;
		}

		++it2;
	}

	return true;
}


// When re-scanning plugins, clear out old module description, but don't delete yet,
// until documents pointers updated to new infos.
// This version applies only to modules provided by the given dll.
void SetAsidePluginData(std::filesystem::path pluginPath)
{
	auto& module_list = ModuleFactory()->module_list;

	for (auto it = module_list.begin(); it != module_list.end(); )
	{
		Module_Info* mi = (*it).second;

		auto mi3 = dynamic_cast<Module_Info3*>(mi);
		if (mi3 && mi3->holder.getPluginPath() == pluginPath)
		{
			mi->setLoadedIntoDatabase(false);

			// assignment (not insert) so a repeated live-update replaces any stale set-aside copy.
			m_in_use_old_module_list[mi->UniqueId()] = std::unique_ptr<Module_Info>(mi);

			it = module_list.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void SetAsideAllPluginData(bool shellPlugins)
{
	auto& module_list = ModuleFactory()->module_list;

	for (auto it = module_list.begin(); it != module_list.end(); )
	{
		Module_Info* mi = (*it).second;

		if (
			(shellPlugins == mi->isShellPlugin() && mi->fromExternalDll())
			|| !mi->isDllAvailable() // discards internal modules not in build (e.g. old inverter).
			)
		{
			//			 _RPTW1(_CRT_WARN, L"%s\n", (*it).first.c_str() );
			std::unique_ptr<Module_Info> ptr(mi);
			m_in_use_old_module_list.insert({ mi->UniqueId(), std::move(ptr) });

			it = module_list.erase(it);
			mi->setLoadedIntoDatabase(false);
		}
		else
		{
			//_RPTW1(_CRT_WARN, L"NOT SET ASIDE %s\n", (*it).first.c_str() );
			++it;
		}
	}
}

// On load, temporary module infos are loaded along with patch, once modules are re-directed to module database, these temporary objects can be deleted.
void DeleteTemporaryModuleDescriptions()
{
	m_in_use_old_module_list.clear();
}

// After plugins scanned, old module descriptions can be deleted..UNLESS a module in the current project
// references it AND it no longer exists on disk (not picked up by new scan).
void RetainMissingModuleDescriptions()
{
	for (auto it = m_in_use_old_module_list.begin(); it != m_in_use_old_module_list.end(); )
	{
		auto mi = (*it).second.get();
		auto res = ModuleFactory()->module_list.insert({ (mi->UniqueId()), mi });

		if (res.second)
		{
			// Insert succeeded. Module wasn't discovered during scan.
			// keep it in case needed by current project, but mark as not available.
//			_RPTW1(_CRT_WARN, L"Retaining %s\n", GetName(mi).c_str());
			mi->setLoadedIntoDatabase();
			mi->SetUnavailable();
			(*it).second.release(); // take ownership of module-info from unique_ptr
			it = m_in_use_old_module_list.erase(it);
		}
		else
		{
			// Insert Failed. Module already detected during scan. No need to retain old copy.
			++it;
		}
	}
}

void RegisterExternalPluginsXml(
	  tinyxml2::XMLDocument* doc
	, const std::wstring& full_win_binary_path
	, const std::filesystem::path& full_mac_binary_path
	, const std::wstring& group_name
	, bool isShellPlugin
)
{
	if (auto shellPlugin = doc->FirstChildElement("ShellPlugin"); shellPlugin)
	{
		assert(false); // should have been checked already. only send 'real' plugin XML here.
		return;
	}

	tinyxml2::XMLNode* pluginList = doc->FirstChildElement("PluginList");

	if (!pluginList) // handle XML without <PluginList> only <Plugin>.
		pluginList = doc;

	if (!pluginList)
		return;

	bool reportedDuplicateModule = false;

	auto factory = CModuleFactory::Instance();

	// The binary this platform actually loads. A universal .sem bundle also carries a Windows DLL
	// under Contents/x86_64-win, so on macOS full_win_binary_path is not the file being registered.
#ifdef _WIN32
	const auto& platformBinaryPath = full_win_binary_path;
#else
	const auto platformBinaryPath = full_mac_binary_path.wstring();
#endif

	// Walk all plugins.
	for (auto PluginElement = pluginList->FirstChildElement("Plugin"); PluginElement; PluginElement = PluginElement->NextSiblingElement("Plugin"))
	{
		// check for existing
		auto plugin_id = Utf8ToWstring(PluginElement->Attribute("id"));

		if (auto mi = factory->GetById(plugin_id) ; mi)
		{
			if (auto sdk3Module = dynamic_cast<Module_Info3*>(mi); sdk3Module)
			{
				// When scanning a mac SEM from SynthEdit.exe,
				// we may have already scanned the Windows SEM, in which case skip creating the module-info and just add the mac file path.
#ifdef _WIN32
				if (!full_mac_binary_path.empty() && sdk3Module->macSemBundlePath.empty())
				{
					sdk3Module->macSemBundlePath = full_mac_binary_path.generic_wstring();
				}
				else // it's a duplicate module.
#endif
				{
					// Can't directly replace a internal modules with a external module using same ID. Need to write replacement code
					if (!reportedDuplicateModule)
					{
						auto plugin_name = Utf8ToWstring(PluginElement->Attribute("name"));
						reportedDuplicateModule = true;
						std::wostringstream oss;
						if(platformBinaryPath == sdk3Module->Filename())
						{
							oss << L"Module registering XML twice!  Name='" <<
								plugin_name << L"' id='" << plugin_id << L"'\n" <<
								L"\t" << platformBinaryPath;
						}
						else
						{
							oss << L"Module FOUND TWICE!, only one loaded. Please remove the oldest.  Name='" <<
								plugin_name << L"' id='" << plugin_id << L"'\n" <<
								L"\t" << platformBinaryPath << L"\n\t" << sdk3Module->Filename();
						}
						SafeMessagebox(0, oss.str().c_str(), L"", MB_OK | MB_ICONSTOP);
					}
				}
			}
			else
			{
				// Joystick Image etc have modern counterparts that are active on on 64-bit.
				// This prevents disruption to 32-bit users.
				if (plugin_id == L"ImageJoystick")
				{
					assert(false); // used?
					return;
				}
				assert(false && "Duplicate Module ID, but is not SDK3");
			}
		}
		else
		{
			auto mi3 = new Module_Info3(platformBinaryPath, group_name);
            
#ifdef _WIN32
            // set filename before scanning, so we can differentiate GMPI from SEM plugins
            mi3->macSemBundlePath = full_mac_binary_path.generic_wstring();
#endif
            
			mi3->ScanXml(PluginElement);
			if (isShellPlugin)
				mi3->setShellPlugin();
            
			auto res = factory->module_list.insert({ mi3->UniqueId(), mi3 });
			assert(res.second); // insert failed.
			mi3->setLoadedIntoDatabase();
		}
	}
}

#if 0
// registers native plugins xml, rejects shell plugins.
// returns [isShellPlugin, Succeeded]
std::pair<bool, bool> RegisterExternalPluginsXml2(
	  tinyxml2::XMLDocument& doc
	, const std::wstring& full_win_binary_path
	, const std::filesystem::path& full_mac_binary_path
	, const std::wstring& group_name
)
{
	if (doc.Error())
	{
		const auto message = formatXmlParseError(doc, {}, full_win_binary_path, XmlSourceKind::moduleResource);
		SafeMessagebox(0, message.c_str(), L"SynthEdit", MB_OK | MB_ICONSTOP);
		return { false, false };
	}

	// rejust shell plugins, they need to be deep scanned.
	if (auto shellPlugin = doc.FirstChildElement("ShellPlugin"); shellPlugin)
		return { true, false };

	// normal SEM, just scan it's XML and we're done.
	RegisterExternalPluginsXml(&doc, full_win_binary_path, full_mac_binary_path, group_name);

	return { false, true };
}
#endif

#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- binary loader
void ScanPluginBinary(
	  PluginHolder& holder
	, const std::wstring& group_name
	, bool isShellPlugin
)
{
	const auto binary_path = holder.getPluginPath();

	try {

		// Plugins with no useful external XML. Scan factory for sub-plugins.
		// GMPI & sem V3 export function
		auto dll_entry_point = holder.getFactory();

		if (!dll_entry_point) // GMPI/SDK3 Plugin?
			return;

		int32_t r{};
		// restrict scope of 'vst_factory' and 'gmpi_factory' so smart pointers RIAA before dll is unloaded
		{
			// Instansiate factory and query sub-plugins.
			gmpi_sdk::mp_shared_ptr<gmpi::IMpShellFactory> vst_factory;
			gmpi_sdk::mp_shared_ptr<gmpi::api::IPluginFactory> gmpi_factory;
			{
				gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> com_object;
				r = dll_entry_point(com_object.asIMpUnknownPtr());

				r = com_object->queryInterface(gmpi::MP_IID_SHELLFACTORY, vst_factory.asIMpUnknownPtr());
				r = com_object->queryInterface((const gmpi::MpGuid&)gmpi::api::IPluginFactory::guid, gmpi_factory.asIMpUnknownPtr());
			}

			if (!vst_factory && !gmpi_factory)
			{
				std::wostringstream oss;
				oss << L"Module missing XML resource, and has no factory\n" << holder.getPluginPath().wstring();
				SafeMessagebox(0, oss.str().c_str(), L"", MB_OK | MB_ICONSTOP);
				return;
			}

			int index = 0;
			while (true)
			{
				MpString s;
                r = gmpi::MP_FAIL;

                if (gmpi_factory)
                {
                    // have to cast GMPI 2 types to GMPI 1 types
                    r = (int32_t)gmpi_factory->getPluginInformation(index++, (gmpi::api::IString*)&s); // FULL XML
                }
                else
                {
                    r = vst_factory->getPluginIdentification(index++, s.getUnknown()); // Summary XML
                }

				if (r != gmpi::MP_OK)
					break;

				tinyxml2::XMLDocument doc2;
				doc2.Parse(s.c_str());

				if (doc2.Error())
				{
					const auto message = formatXmlParseError(doc2, s.c_str(), binary_path.wstring(), XmlSourceKind::moduleFactory);
					SafeMessagebox(0, message.c_str(), L"SynthEdit", MB_OK | MB_ICONSTOP);
					return;
				}
#ifdef _WIN32
				RegisterExternalPluginsXml(&doc2, binary_path, {}, group_name, isShellPlugin);
#else
				RegisterExternalPluginsXml(&doc2, {}, binary_path, group_name, isShellPlugin);
#endif
			}
		}
	}
	catch (...)
	{
        // exception scanning module
        std::cout << "EXCEPTION SCANNING MODULE: " << holder.getPluginPath().string() << std::endl;
	}
}
#endif // SE_NO_EXTERNAL_MODULES

#ifdef _WIN32
#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- binary loader (windows arm)
void ScanStandaloneSem(
	  const std::wstring& group_name
	, std::filesystem::path binary_path
	, bool scanVstsOnly
)
{
	PluginHolder holder(binary_path);
	holder.load();

	if (!holder.isLoaded())
	{
		_RPT1(_CRT_WARN, "Failed to load plugin: %s\n", binary_path.string().c_str());
		return;
	}

	bool isShellPlugin{};

	// extract XML from resources, if any.
	HINSTANCE hInst = (HINSTANCE)holder.getHandle();
	HRSRC hRsrc = ::FindResource(hInst,
		MAKEINTRESOURCE(1), // ID
		L"GMPXML");			// type GMPI XML

	if (hRsrc)
	{
		const BYTE* lpRsrc = (BYTE*)LoadResource(hInst, hRsrc);

		if (!lpRsrc)
		{
			std::wostringstream oss;
			oss << L"Module missing XML resource\n" << binary_path.wstring();
			SafeMessagebox(0, oss.str().c_str(), L"", MB_OK | MB_ICONSTOP);
			return;
		}

		const auto xmlSize = SizeofResource(hInst, hRsrc);
		const BYTE* locked_mem = (BYTE*)LockResource((HANDLE)lpRsrc);
		std::string xmlFile((char*)locked_mem, xmlSize);
		// cleanup
		UnlockResource((HANDLE)lpRsrc);
		FreeResource((HANDLE)lpRsrc);

		tinyxml2::XMLDocument doc;
		doc.Parse(xmlFile.c_str());

		if (doc.Error())
		{
			const auto message = formatXmlParseError(doc, xmlFile, binary_path.wstring(), XmlSourceKind::moduleResource);
			SafeMessagebox(0, message.c_str(), L"SynthEdit", MB_OK | MB_ICONSTOP);
			return;
		}

		isShellPlugin = (nullptr != doc.FirstChildElement("ShellPlugin"));

		if (!isShellPlugin && !scanVstsOnly)
		{
			RegisterExternalPluginsXml(&doc, binary_path, {}, group_name, isShellPlugin);
			return;
		}
	}

	if (scanVstsOnly != isShellPlugin)
		return;

	// we need to scan binary because it's either a shell plugin, or a regular plugin with no XML resource.
	ScanPluginBinary(holder, group_name, isShellPlugin);
}
#endif // SE_NO_EXTERNAL_MODULES
#endif

std::filesystem::path firstFileIn(std::filesystem::path folder, std::string extension)
{
	if (!std::filesystem::exists(folder))
		return{};

	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
	{
		if (ec)
			break;

		if (!entry.is_regular_file())
			continue;

		if (extension.empty() || entry.path().extension() == extension)
		{
			return entry;
			break;
		}
	}
	return {};
}

// The module-description XML in a bundle's Resources folder. A SynthEdit-exported
// plugin keeps several engine resources there (dsp.se.xml, parameters.se.xml, ...)
// alongside its descriptor, so name the descriptor explicitly and never mistake one
// of the '*.se.xml' resources for a module description.
std::filesystem::path moduleXmlIn(const std::filesystem::path& resourcesFolder, const std::string& bundleStem, bool isGmpi)
{
	if (const auto gmpiDescriptor = resourcesFolder / "plugin.gmpi.xml"; std::filesystem::exists(gmpiDescriptor))
		return gmpiDescriptor;

	// A .gmpi may ship its description as a resource, exactly as a .sem does. But
	// a plug-in is also entitled to keep its OWN files here -- TIDE carries the XML
	// of its child modules (Converters.xml, VaFilters.xml, ...) -- and those are
	// private assets, not this bundle's description. Taking whichever .xml the
	// directory iterator happened to yield first registered them as modules, and
	// because they are SDK3 files read on the GMPI path, every datatype="string"
	// pin (wide in SDK3) was recorded as the GMPI String (UTF-8). That silently
	// retyped every std::wstring pin in the Converters module, so "SE TextToText8"
	// arrived as Text8->Text8, converted nothing, and ug_base::connect inserted
	// converters forever until the stack died.
	//
	// So for .gmpi the descriptor must be named after the bundle. Measured on one
	// mac: of 64 installed bundles, 62 already ship only a self-named xml and 30
	// ship none at all -- the only two that break the rule are the two at fault.
	// .sem keeps its historic behaviour: the problem has never arisen there and the
	// format is deprecated.
	if (isGmpi)
	{
		if (const auto selfNamed = resourcesFolder / (bundleStem + ".xml"); std::filesystem::exists(selfNamed))
			return selfNamed;

		return {};
	}

	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(resourcesFolder, ec))
	{
		if (ec)
			break;

		if (!entry.is_regular_file())
			continue;

		const auto& p = entry.path();
		if (p.extension() != ".xml" || p.stem().extension() == ".se")
			continue;

		return p;
	}
	return {};
}

#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- binary loader
void ScanBundle(const std::wstring& group_name, const std::filesystem::path& bundle_path, bool scanVstsOnly)
{
	const auto FileExtension = bundle_path.extension().string();

	if (FileExtension != ".sem" != 0 && FileExtension != ".gmpi")
		return;

	const auto Contents = bundle_path / "Contents";

	// scan for binaries
	auto binaryWin = firstFileIn(Contents / "x86_64-win", {});
#if defined(_WIN32) || defined(__APPLE__)
	auto binaryMac = firstFileIn(Contents / "MacOS", {});
#else
	// Linux keeps the shared object in Contents/x86_64-linux. Like macOS it's the
	// bundle folder that gets registered, not the binary — PluginHolder::load digs
	// the platform binary out of it.
	auto binaryMac = firstFileIn(Contents / "x86_64-linux", {});
#endif

	std::filesystem::path macBundlePath;
	if (!binaryMac.empty())
		macBundlePath = bundle_path;

	// if we can find an external XML file, we can use that without having to load the dll.
	// scanning mac plugin on Windows will fail if they don't have discrete xml file
	bool isShellPlugin = false;
	if (auto xmlFile = moduleXmlIn(Contents / "Resources", bundle_path.stem().string(), FileExtension == ".gmpi"); !xmlFile.empty())
	{
		tinyxml2::XMLDocument doc;
		doc.LoadFile(toString(xmlFile).c_str());

		if (doc.Error())
		{
			const auto message = formatXmlParseErrorFromFile(doc, xmlFile);
			SafeMessagebox(0, message.c_str(), L"SynthEdit", MB_OK | MB_ICONSTOP);
			return;
		}

		isShellPlugin = (nullptr != doc.FirstChildElement("ShellPlugin"));

		if (!isShellPlugin && !scanVstsOnly)
		{
			RegisterExternalPluginsXml(&doc, binaryWin.wstring(), macBundlePath, group_name, isShellPlugin);
			return;
		}
	}

	if (scanVstsOnly != isShellPlugin)
		return;

	// we need to scan binary because it's either a shell plugin, or a regular plugin with no external XML resource.
	PluginHolder holder(
#ifdef _WIN32
		binaryWin
#else
		macBundlePath
#endif
	);
	holder.load();

	if (!holder.isLoaded())
	{
		_RPT1(_CRT_WARN, "Failed to load plugin: %s\n", holder.getPluginPath().c_str());
		return;
	}

	ScanPluginBinary(holder, group_name, isShellPlugin);
}
#endif // SE_NO_EXTERNAL_MODULES

#if 0
#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- binary loader
void ScanFile(
	  const std::wstring& group_name
	, std::filesystem::path binary_path
	, bool scanVstsOnly
)
{
#ifdef _DEBUG
	const auto ext = binary_path.extension().string();
	assert(ext == ".sem" || ext == ".gmpi"); // SEMs only.
#endif

	try
	{
		PluginHolder holder(binary_path);
		holder.load();

		if (!holder.isLoaded())
		{
			_RPT1(_CRT_WARN, "Failed to load plugin: %s\n", binary_path.string().c_str());
			return;
		}

#ifdef _WIN32
		// Use XML data from imbedded resource to get list of plugins
		HINSTANCE hInst = (HINSTANCE)holder.getHandle();
		HRSRC hRsrc = ::FindResource(hInst,
			MAKEINTRESOURCE(1), // ID
			L"GMPXML");			// type GMPI XML

		if (hRsrc)
		{
			const BYTE* lpRsrc = (BYTE*)LoadResource(hInst, hRsrc);

			if (lpRsrc == 0)
			{
				std::wostringstream oss;
				oss << L"Module missing XML resource\n" << binary_path.wstring();
				SafeMessagebox(0, oss.str().c_str(), L"", MB_OK | MB_ICONSTOP);
				return;
			}

			const auto xmlSize = SizeofResource(hInst, hRsrc);
			const BYTE* locked_mem = (BYTE*)LockResource((HANDLE)lpRsrc);
			std::string xmlFile((char*)locked_mem, xmlSize);
			// cleanup
			UnlockResource((HANDLE)lpRsrc);
			FreeResource((HANDLE)lpRsrc);

			tinyxml2::XMLDocument doc;
			doc.Parse(xmlFile.c_str());

			auto [isShellPlugin, Succeeded] = RegisterExternalPluginsXml2(doc, binary_path, {}, group_name);
			if (!Succeeded || !isShellPlugin)
				return;
		}
#endif

		// Shell plugins
		// GMPI & sem V3 export function
		auto dll_entry_point = holder.getFactory();

		if (!dll_entry_point) // GMPI/SDK3 Plugin?
			return;

		int32_t r{};
		// restrict scope of 'vst_factory' and 'gmpi_factory' so smart pointers RIAA before dll is unloaded
		{
			// Instansiate factory and query sub-plugins.
			gmpi_sdk::mp_shared_ptr<gmpi::IMpShellFactory> vst_factory;
			gmpi_sdk::mp_shared_ptr<gmpi::api::IPluginFactory> gmpi_factory;
			{
				gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> com_object;
				r = dll_entry_point(com_object.asIMpUnknownPtr());

				r = com_object->queryInterface(gmpi::MP_IID_SHELLFACTORY, vst_factory.asIMpUnknownPtr());
				r = com_object->queryInterface((const gmpi::MpGuid&)gmpi::api::IPluginFactory::guid, gmpi_factory.asIMpUnknownPtr());
			}

			if (!vst_factory && !gmpi_factory)
			{
				std::wostringstream oss;
				oss << L"Module missing XML resource, and has no factory\n" << binary_path.wstring();
				SafeMessagebox(0, oss.str().c_str(), L"", MB_OK | MB_ICONSTOP);
				return;
			}
// no asssumes vst3 wrapper is SEM not GMPI. all plugins with internal XML are "shell plugins"
			// if we found VST shell but we're only scanning SEMs, exit.
			if ((vst_factory && !scanVstsOnly) || (gmpi_factory && scanVstsOnly))
				return;

			int index = 0;
			while (true)
			{
				MpString s;

				if (gmpi_factory)
				{
					// have to cast GMPI 2 types to GMPI 1 types
					r = (int32_t)gmpi_factory->getPluginInformation(index++, (gmpi::api::IString*)&s); // FULL XML
				}
				else
				{
					r = vst_factory->getPluginIdentification(index++, s.getUnknown()); // Summary XML
				}

				if (r != gmpi::MP_OK)
					break;

				tinyxml2::XMLDocument doc2;
				doc2.Parse(s.c_str());

				if (doc2.Error())
				{
					const auto message = formatXmlParseError(doc2, s.c_str(), binary_path.wstring(), XmlSourceKind::moduleFactory);
					SafeMessagebox(0, message.c_str(), L"SynthEdit", MB_OK | MB_ICONSTOP);
					return;
				}
#ifdef _WIN32
				RegisterExternalPluginsXml(&doc2, binary_path, {}, group_name, true);
#else
				RegisterExternalPluginsXml(&doc2, {}, binary_path, group_name, true);
#endif
			}
		}
	}
	catch (...)
	{
		return;
	}
}
#endif // SE_NO_EXTERNAL_MODULES
#endif

#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- binary loader (non-windows arms)
#ifndef _WIN32
#if defined(__APPLE__)
// Standalone (non-bundled) SEMs only exist on Windows and Linux. No-op stub on
// macOS (where modules are always bundles) so ScanFolder stays macro-free.
static void ScanStandaloneSem(const std::wstring&, const std::filesystem::path&, bool)
{
}
#else
// Linux: modules are plain shared objects. The XML that Windows embeds as a
// resource is copied beside the binary at build time (<binary>.xml); prefer
// that, else query the factory directly (same tail path as the Windows version).
static void ScanStandaloneSem(
	  const std::wstring& group_name
	, const std::filesystem::path& binary_path
	, bool scanVstsOnly
)
{
	if (scanVstsOnly)
		return; // shell (VST) plugins aren't shipped as standalone SEMs here.

	// sidecar XML (SDK3-style modules can't serve XML from their factory).
	{
		auto xmlPath = binary_path;
		xmlPath += ".xml";

		tinyxml2::XMLDocument doc;
		if (std::filesystem::exists(xmlPath) &&
			tinyxml2::XML_SUCCESS == doc.LoadFile(xmlPath.string().c_str()))
		{
			if (nullptr == doc.FirstChildElement("ShellPlugin"))
			{
				RegisterExternalPluginsXml(&doc, {}, binary_path, group_name, false);
				return;
			}
		}
	}

	PluginHolder holder(binary_path);
	holder.load();

	if (!holder.isLoaded())
	{
		_RPT1(_CRT_WARN, "Failed to load plugin: %s\n", binary_path.string().c_str());
		return;
	}

	ScanPluginBinary(holder, group_name, false);
}
#endif
#endif
#endif // SE_NO_EXTERNAL_MODULES

void ScanFolder(const std::filesystem::path& p_path, const std::string& p_extensions, const std::wstring& sub_menu, bool scanVstsOnly)
{
	std::error_code ec;
	for (const auto& dirEntry : std::filesystem::directory_iterator(p_path, ec))
	{
		if (ec)
			break;

		const auto& entryPath = dirEntry.path();
		const auto extension = entryPath.extension().string();
		const bool isTargetExt = !extension.empty() && p_extensions.find(extension) != std::string::npos;

		if (!dirEntry.is_directory(ec))
		{
			if (!isTargetExt)
				continue;

			if (extension == ".synthedit" || extension == ".syntheditprefab" || extension == ".seprefab")
			{
				const auto prefab = std::filesystem::path(sub_menu) / entryPath.filename();
				ModuleFactory()->PrefabFileNames.push_back(prefab.wstring());
			}
#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- the binary arm; the prefab arm above stays
			else
			{
				// non-bundled SEMs only supported on Windows for historic reasons; stub on other platforms.
				ScanStandaloneSem(sub_menu, entryPath, scanVstsOnly);
			}
#endif // SE_NO_EXTERNAL_MODULES
		}
#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- a DIRECTORY named *.sem/*.gmpi/*.synthedit
		else if (isTargetExt)
		{
			// bundle (.sem / .gmpi) — treated as a single unit.
			ScanBundle(sub_menu, entryPath, scanVstsOnly);
		}
#endif // SE_NO_EXTERNAL_MODULES
		else
		{
			// regular sub-folder: descend, extending the menu hierarchy.
			const auto sub_sub_menu = (std::filesystem::path(sub_menu) / entryPath.filename()).wstring();
			ScanFolder(entryPath, p_extensions, sub_sub_menu, scanVstsOnly);
		}
	}
}

// Scan a single module (e.g. one just arrived via live-update) and register its
// module descriptions with the factory. Single-file companion to ScanFolder.
#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- binary loader
void ScanFile(const std::wstring& group_name, const std::filesystem::path& binary_path)
{
	if (std::filesystem::is_directory(binary_path))
	{
		// bundle (.sem / .gmpi) — treated as a single unit.
		ScanBundle(group_name, binary_path, false);
	}
	else
	{
#ifdef _DEBUG
		const auto ext = binary_path.extension().string();
		assert(ext == ".sem" || ext == ".gmpi"); // SEMs only.
#endif
		// non-bundled SEMs only supported on Windows for historic reasons; stub on other platforms.
		ScanStandaloneSem(group_name, binary_path, false);
	}
}
#endif // SE_NO_EXTERNAL_MODULES

#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- binary loader
std::wstring UnloadDll(std::wstring dllShortName)
{
	std::filesystem::path fullFilename;

	for (auto& m : ModuleFactory()->module_list)
	{
		auto mi3 = dynamic_cast<Module_Info3*>(m.second);
		if(!mi3)
			continue;

		auto& dllinfo = mi3->holder;
		if (fullFilename.empty())
		{
			if (dllinfo.getPluginPath().filename() == dllShortName)
				fullFilename = dllinfo.getPluginPath();
		}

		if (!fullFilename.empty() && fullFilename == dllinfo.getPluginPath())
			dllinfo.unload();
	}
	return fullFilename.wstring();
}
#endif // SE_NO_EXTERNAL_MODULES

#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- binary loader
void ReloadDll(std::filesystem::path dllPath)
{
	for (auto& m : ModuleFactory()->module_list)
	{
		auto mi3 = dynamic_cast<Module_Info3*>(m.second);
		if(!mi3)
			continue;

		auto& dllinfo = mi3->holder;
		if (dllPath == dllinfo.getPluginPath())
			dllinfo.load();
	}
}
#endif // SE_NO_EXTERNAL_MODULES

void ExportModuleData(tinyxml2::XMLElement* doc, ExportFormatType format)
{
	auto ModulesElement = doc->GetDocument()->NewElement("PluginList");
	doc->LinkEndChild(ModulesElement);

	for (auto& m : ModuleFactory()->module_list)
	{
		if (m.second->getSerialiseFlag())
		{
			ExportModuleInfo(m.second, ModulesElement, format);
		}
	}
}

#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- SEM cache
void StoreModuleData()
{
	// Modules only to be cached if dll present on disk.
	ModuleFactory()->ClearSerialiseFlags();
	CSynthEditDocBase::serializingMode = SERT_SEM_CACHE;

	std::filesystem::path settingsPath(getSettingsFolder());
	settingsPath.append(L"SynthEdit");

	// Create folder if not already.
//	_wmkdir(commonApplicationData.c_str());
	CreateFolderRecursive(settingsPath.generic_wstring());

	{   // new XML way
		// C:\ProgramData\SynthEdit\SynthEdit\Plugin-Cache.xml
		auto filename = settingsPath.append(SemCacheName());

		tinyxml2::XMLDocument xmlDocument;
		xmlDocument.LinkEndChild(xmlDocument.NewDeclaration());
		auto doc_xml = xmlDocument.NewElement("Document");
		xmlDocument.LinkEndChild(doc_xml);
		
		doc_xml->SetAttribute("file_format", (int)XML_FILE_FORMAT_VERSION_NUM);
		doc_xml->SetAttribute("build_number", (int)SE_APP_BUILD_NUMBER);

		ExportModuleInfo(doc_xml, SAT_SEM_CACHE);

		// VST Plugins and Prefabs.
		auto prefabs_xml = xmlDocument.NewElement("Prefabs");
		doc_xml->LinkEndChild(prefabs_xml);
		for (auto it = ModuleFactory()->PrefabFileNames.begin(); it != ModuleFactory()->PrefabFileNames.end(); ++it)
		{
			auto prefab_xml = xmlDocument.NewElement("Prefab");
			prefab_xml->SetAttribute("name", WStringToUtf8(*it).c_str());
			prefabs_xml->LinkEndChild(
				prefab_xml
			);
		}

		xmlDocument.SaveFile(filename.generic_string().c_str());
	}

	_RPTW1(_CRT_WARN, L"Serialized prefabs, %d\n", ModuleFactory()->PrefabFileNames.size());
	CSynthEditDocBase::serializingMode = SERT_UNSET;
}
#endif // SE_NO_EXTERNAL_MODULES

#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- SEM cache
bool ClearModuleDataCache()
{
	std::filesystem::path cacheFilename = std::filesystem::path(getSettingsFolder()) / L"SynthEdit" / SemCacheName();
	std::error_code ec;
	if (std::filesystem::exists(cacheFilename, ec))
	{
		std::filesystem::remove(cacheFilename, ec);
		return !std::filesystem::exists(cacheFilename, ec);
	}
	return true; // file doesn't exist, so 'cleared'
}
#endif // SE_NO_EXTERNAL_MODULES

#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- the SEM cache exists only to avoid rescanning binaries
bool LoadModuleData()
{
	CSynthEditDocBase::serializingMode = SERT_SEM_CACHE;

	auto cacheFilename = std::filesystem::path(getSettingsFolder()) / L"SynthEdit" / SemCacheName();

	auto filename = cacheFilename.generic_string();

	{   // new XML way
		tinyxml2::XMLDocument xmlDocument;
		xmlDocument.LoadFile(filename.c_str());

		if (xmlDocument.Error())
		{
			// no cache so, re-generate.
			return false;
		}

		auto doc_xml = xmlDocument.FirstChildElement("Document");

		int fileFormatVersion = 0;
		doc_xml->QueryIntAttribute("file_format", &fileFormatVersion);
		if (fileFormatVersion < 150000) // initial file format screwed up module database (missing flags, no differention between Module_Info classes) 
		{
			// re-generate.
			return false;
		}

		// Check if build number changed, requiring a rescan
		int cachedBuildNumber = 0;
		doc_xml->QueryIntAttribute("build_number", &cachedBuildNumber);
		if (cachedBuildNumber != SE_APP_BUILD_NUMBER)
		{
			return false;
		}

		CDocOb::m_loading_version = fileFormatVersion;

		ImportModuleInfo(doc_xml, SAT_SEM_CACHE, fileFormatVersion);

		if (ModuleFactory()->PrefabFileNames.empty())
		{
			auto prefabs = doc_xml->FirstChildElement("Prefabs");
			for (auto prefab = prefabs->FirstChildElement("Prefab"); prefab; prefab = prefab->NextSiblingElement())
			{
				ModuleFactory()->PrefabFileNames.push_back(Utf8ToWstring(prefab->Attribute("name")));
			}
		}
		else
		{
			_RPT0(_CRT_WARN, "Prefabs should be empty! (unless loading SynthEdit.som recursively)\n");
			// todo guard against loading module info recursively, if only to save time
		}
	}

	CDocOb::m_loading_version = FILE_FORMAT_VERSION_NUM; // important
	CSynthEditDocBase::serializingMode = SERT_UNSET;

	return true;
}
#endif // SE_NO_EXTERNAL_MODULES

bool initializedFromXml = {};

// was Import()
void ImportModuleInfo(tinyxml2::XMLElement* documentE, ExportFormatType fileType, int fileFormatVersion)
{
	auto factory = CModuleFactory::Instance();

	assert(m_in_use_old_module_list.empty());

	const bool loadingCache = (fileType == SAT_SEM_CACHE);

	// Start each load with an empty "will be upgraded" list so the consolidated dialog
	// (showUpgradeMessage) only ever lists modules from the project being loaded now,
	// never stale residue from a prior paste/load of the shared static list.
	CSynthEditDocBase::upgradeLoadList.clear();

	// initial file format screwed up module database (missing flags, no differention between Module_Info classes).
	// this was fixed in 1.5 150000
	const bool documentInfoUnreliable = fileFormatVersion < 150000;
	
#if _DEBUG
	if (documentInfoUnreliable)
	{
		_RPT0(0, "Warning: loading old file format. Module info from file will be discarded.\n");
	}
#endif

	// load module infos stored in file, add any unknown ones to database
	tinyxml2::XMLNode* pluginList = documentE->FirstChildElement("PluginList");

	if (!pluginList) // handle XML without <PluginList> only <Plugin>.
		pluginList = documentE;

	if (!pluginList)
		return;

	// NOTE: when pasting. There will be no module-info. It's assumed to already exist.
	for (auto pluginE = pluginList->FirstChildElement("Plugin"); pluginE; pluginE = pluginE->NextSiblingElement("Plugin"))
	{
		const auto plugin_id = Utf8ToWstring(pluginE->Attribute("id"));

		int classTypeId = 0; // Module_Info3 by default
		pluginE->QueryIntAttribute("class", &classTypeId);

		// Older XML file format omitted which module_info class the module uses. (Module_Info, Module_Info3, Module_Info3_internal)
		// we can only instansiate these if we can get that info from local database.
		if (0 == classTypeId && 150000 > fileFormatVersion)
		{
			auto mif = ModuleFactory()->GetById(plugin_id);

			if (mif)
			{
				classTypeId = mif->getClassType();
			}
			else
			{
				if (loadingCache)
					continue;

				_RPTWN(0, L"ERROR: Can't determine module-into class type for %s\n", plugin_id.c_str());
				throw std::runtime_error("Can't load old file-format without module-info available.");
			}
		}

		Module_Info* mi{};
		Module_Info3* mi3{};

		auto fileX = pluginE->Attribute("file"); // Cache-only
		if (fileX || classTypeId == 0)
		{
			mi = mi3 = new Module_Info3(Utf8ToWstring(fileX));
		}
		else
		{
			switch (classTypeId)
			{
			case 2:
				mi = new Module_Info3_internal(plugin_id.c_str());
				break;

			case 1:
				// meh:	mi = new Module_Info_Plugin();
				// we can ignore that they are VST2 and just treat them as any module which is not available
				[[fallthrough]];

			default:
				mi = new Module_Info(plugin_id);
				break;
			}

			mi->SetUnavailable(); // until creation function registered.
		}

		mi->ScanXml(pluginE);

		// insert in module list, only if not already present
		auto res = factory->module_list.insert({ mi->UniqueId(), mi });

		if (res.second) // insert OK, module not previously registered
		{
			mi->setLoadedIntoDatabase();

			// when loading cache, not finding existing info is fine.
			if (!loadingCache)
			{
				CSynthEditDocBase::CantLoad(mi->UniqueId());
#if defined( _DEBUG )
				_RPTW1(_CRT_WARN, L"%s: Module info not avail, using stored info\n", mi->UniqueId().c_str());
#endif
			}
		}
		else
		{
			auto localModuleInfo = factory->GetById(mi->UniqueId());

			// Check for wrapped VST2 plugins that are not available, only summary info.
			// In this case we prefer to use the full module information from the project file.
			if (localModuleInfo->isSummary() && !localModuleInfo->OnDemandLoad())
			{
				auto it = factory->module_list.find(localModuleInfo->UniqueId());
				factory->module_list.erase(it);
				delete localModuleInfo;

				auto res2 = factory->module_list.insert({ mi->UniqueId(), mi });

				assert(res2.second);
				{
					// insert OK, module not previously registered
					mi->setLoadedIntoDatabase();
				}
			}
			else
			{
				// if we're loading an early document with broken module-info for internal modules, discard it. (it will be retained only if no local version exist)
				// this is because early documents didn't record special flags like IO_SETABLE_OUTPUT and IO_CONTAINER_PLUG on plugs, or 'class' to signal what Module_info class to instansiate.
				// external modules don't tend to use these flags, so it's better to retain the info from the document to avoid crashes due to incompatible pins (updated modules)
				bool retainInfoFromDocument =
					!loadingCache &&
					(!documentInfoUnreliable || (localModuleInfo->getClassType() == 0 && mi->getClassType() == 0));

				if (retainInfoFromDocument)
				{
					// Check stored pin data compatible with local version of module (rough check). Ck_Waveshaper1 has this problem. two versions with same ID but different pin count.
					auto storedModuleInfo = mi;
					if (!isCompatibleWith(storedModuleInfo, localModuleInfo))
					{
						mi->m_incompatible_with_current_module = true;

						// Collect for a single consolidated dialog (showUpgradeMessage) rather than
						// popping one modal box per module - painful for projects with many old modules.
						CSynthEditDocBase::Upgrade(GetName(localModuleInfo));
					}

					// XML format does not record these flags used only on legacy internal modules (e.g. Slider).
					// // we pick them up from the auto-registered localModuleInfos which are created automatically at startup.
					// CF_OLD_STYLE_LISTINTERFACE affects the order in which pins are added to the UG. Normally it's GUI first, but for Slider, it's strictly in the declared order (GUI last).
					// Otherwise DSP connection get scrambled.
					storedModuleInfo->SetFlags(storedModuleInfo->GetFlags() | (localModuleInfo->GetFlags() & (CF_OLD_STYLE_LISTINTERFACE | CF_IO_MOD | CF_NOTESOURCE | CF_DONT_EXPAND_CONTAINER | CF_IS_FEEDBACK)));

					// store pointer so object can be deleted after load.
					mi->setLoadedIntoDatabase(false);
					m_in_use_old_module_list.insert({ mi->UniqueId(), std::unique_ptr<Module_Info>{mi} });
				}
				else
				{
					delete mi;
				}
			}
		}
	}
#if 0 // fixed with file version number
	// fix screwup where projects written before 2023 omitted "IO_CONTAINER_PLUG" on container/IO Mod spare pins
	// and 'IO_SETABLE_OUTPUT' on 'Fixed Values'
	{
		bool fileFormatMissingFlags = false;
		{
			auto it = m_in_use_old_module_list.find(L"Container");
			if (it != m_in_use_old_module_list.end())
			{
				auto it2 = (*it).second->plugs.find(0);
				if (it2 != (*it).second->plugs.end())
				{
					assert((*it2).second->GetName() == L"Spare");
					fileFormatMissingFlags = 0 == ((*it2).second->GetFlags() & IO_CONTAINER_PLUG);
				}
			}
		}

		if (fileFormatMissingFlags)
		{
			// revert to local Container and 'fixed values' module database (not one from document).
			for (auto moduletype : { L"Container", L"IO Mod", L"Fixed Values",L"SE:Fixed Values_Text", L"SE:Fixed Values_Float" , L"SE:Fixed Values_Int", L"SE:Fixed Values_Bool" })
			{
				if (auto it2 = m_in_use_old_module_list.find(moduletype); it2 != m_in_use_old_module_list.end())
				{
					m_in_use_old_module_list.erase(it2);
				}
			}
		}
	}
#endif
}

void SaveModuleInfoPinXml(InterfaceObject* pin, ExportFormatType format, TiXmlElement* DspXml, int& expectedId)
{
	TiXmlElement* pinXml = new TiXmlElement("Pin");
	DspXml->LinkEndChild(pinXml);

	if (expectedId != pin->getPlugDescID())
	{
		expectedId = pin->getPlugDescID();
		pinXml->SetAttribute("id", expectedId);
	}

	pinXml->SetAttribute("name", WStringToUtf8(pin->GetName()));
	pinXml->SetAttribute("datatype", XmlStringFromDatatype(pin->GetDatatype()));
	if (pin->GetDatatype() == DT_FSAMPLE)
	{
		pinXml->SetAttribute("rate", "audio");
	}

	const char* direction = 0; // or "in" (default)
	if (pin->GetDirection() == DR_OUT)
	{
		direction = "out";
	}
	else
	{
		auto defaultString = uniformDefaultString(pin->GetDefaultVal(), pin->GetDatatype());
		if (!defaultString.empty())
		{
			// special-case Volts, need divide by 10
			if (pin->GetDatatype() == DT_FSAMPLE)
			{
				// divide default by 10 (to Volts). DoubleToString() removes trailing zeros.
				defaultString = DoubleToString(0.1f * StringToFloat(defaultString));
			}

			pinXml->SetAttribute("default", WStringToUtf8(defaultString));
		}
	}

	if (direction)
	{
		pinXml->SetAttribute("direction", direction);
	}

	const auto pinFlags = pin->GetFlags();
	for (auto& pf : IO_flagNames)
	{
		if (0 != (pf.flag & pinFlags))
		{
			pinXml->SetAttribute(pf.name, "true");
		}
	}
	// exception
	if ((pin->GetFlags() & IO_PARAMETER_SCREEN_ONLY) != 0)
	{
		pinXml->SetAttribute("isMinimised", "true");
	}

#if 0
	if (pin->DisableIfNotConnected(0))
	{
		pinXml->SetAttribute("private", "true");
	}
	if (pin->isRenamable(0))
	{
		pinXml->SetAttribute("autoRename", "true");
	}
	if (pin->is_filename(0))
	{
		pinXml->SetAttribute("isFilename", "true");
	}
	if ((pin->GetFlags() & IO_SETABLE_OUTPUT) != 0)
	{
		pinXml->SetAttribute("settableOutput", "true");
	}
	if ((pin->GetFlags() & IO_LINEAR_INPUT) != 0)
	{
		pinXml->SetAttribute("linearInput", "true");
	}
	if ((pin->GetFlags() & IO_IGNORE_PATCH_CHANGE) != 0)
	{
		pinXml->SetAttribute("ignorePatchChange", "true");
	}
	if ((pin->GetFlags() & IO_AUTODUPLICATE) != 0)
	{
		pinXml->SetAttribute("autoDuplicate", "true");
	}
	if ((pin->GetFlags() & IO_CONTAINER_PLUG) != 0)
	{
		pinXml->SetAttribute("isContainerIoPlug", "true");
	}
	//if ((pin->GetFlags() & IO_PARAMETER_SCREEN_ONLY) != 0)
	//{
	//	pinXml->SetAttribute("noAutomation", "true");
	//}
	if ((pin->GetFlags() & IO_MINIMISED) != 0 || (pin->GetFlags() & IO_PARAMETER_SCREEN_ONLY) != 0)
	{
		pinXml->SetAttribute("isMinimised", "true");
	}
	if ((pin->GetFlags() & IO_PAR_POLYPHONIC) != 0)
	{
		pinXml->SetAttribute("isPolyphonic", "true");
	}
	if ((pin->GetFlags() & IO_AUTOCONFIGURE_PARAMETER) != 0)
	{
		pinXml->SetAttribute("autoConfigureParameter", "true");
	}
#endif
	if (pin->getParameterId() != -1)
	{
		pinXml->SetAttribute("parameterId", (int)pin->getParameterId());

		if (pin->getParameterFieldId() != FT_VALUE)
		{
			if (format == SAT_VST3)
			{
				pinXml->SetAttribute("parameterField", pin->getParameterFieldId()); // plain int for speed in compiled plugin.
			}
			else
			{
				pinXml->SetAttribute("parameterField", XmlStringFromParameterField(pin->getParameterFieldId()));
			}
		}
	}

	HostControls hostControlId = pin->getHostConnect();
	if (hostControlId != HC_NONE)
	{
		pinXml->SetAttribute("hostConnect", WStringToUtf8(GetHostControlName(hostControlId)));
		if (pin->getParameterFieldId() != FT_VALUE)
		{
			//			pinXml->SetAttribute("parameterField", XmlStringFromParameterField(pin->getParameterFieldId(0)));
			if (format == SAT_VST3)
			{
				pinXml->SetAttribute("parameterField", pin->getParameterFieldId()); // plain int for speed in compiled plugin.
			}
			else
			{
				pinXml->SetAttribute("parameterField", XmlStringFromParameterField(pin->getParameterFieldId()));
			}
		}
	}

	if (!pin->GetEnumList().empty())
	{
		pinXml->SetAttribute("metadata", WStringToUtf8(pin->GetEnumList()));
	}
}

void SaveModuleInfoPinXml(InterfaceObject* pin, ExportFormatType format, tinyxml2::XMLElement* DspXml, int& expectedId)
{
	auto  pinXml = DspXml->GetDocument()->NewElement("Pin");
	DspXml->LinkEndChild(pinXml);

	if (expectedId != pin->getPlugDescID())
	{
		expectedId = pin->getPlugDescID();
		pinXml->SetAttribute("id", expectedId);
	}

	pinXml->SetAttribute("name", WStringToUtf8(pin->GetName()).c_str());

	if (pin->GetDatatype() == DT_STRUCT && !pin->getClassName().empty())
	{
		pinXml->SetAttribute("datatype", ("struct:" + pin->getClassName()).c_str());
	}
	else if (pin->GetDatatype() == DT_OBJECT && !pin->getClassName().empty())
	{
		pinXml->SetAttribute("datatype", ("object:" + pin->getClassName()).c_str());
	}
	else
	{
		// bare "struct" / "object" (no subtype) round-trip via the datatype table.
		pinXml->SetAttribute("datatype", XmlStringFromDatatype(pin->GetDatatype()).c_str());
		if (pin->GetDatatype() == DT_FSAMPLE)
		{
			pinXml->SetAttribute("rate", "audio");
		}
	}

	const char* direction = 0; // or "in" (default)
	if (pin->GetDirection() == DR_OUT)
	{
		direction = "out";
	}
	else
	{
		auto defaultString = uniformDefaultString(pin->GetDefaultVal(), pin->GetDatatype());
		if (!defaultString.empty())
		{
			// special-case Volts, need divide by 10
			if (pin->GetDatatype() == DT_FSAMPLE)
			{
				// divide default by 10 (to Volts). DoubleToString() removes trailing zeros.
				defaultString = DoubleToString(0.1f * StringToFloat(defaultString));
			}

			pinXml->SetAttribute("default", WStringToUtf8(defaultString).c_str());
		}
	}

	if (direction)
	{
		pinXml->SetAttribute("direction", direction);
	}

	if (pin->DisableIfNotConnected())
	{
		pinXml->SetAttribute("private", "true");
	}
	if (pin->isRenamable())
	{
		pinXml->SetAttribute("autoRename", "true");
	}
	if (pin->is_filename())
	{
		pinXml->SetAttribute("isFilename", "true");
	}
	if ((pin->GetFlags() & IO_FILENAME_WRITABLE) != 0)
	{
		pinXml->SetAttribute("isFilenameWritable", "true");
	}
	if ((pin->GetFlags() & IO_SETABLE_OUTPUT) != 0)
	{
		pinXml->SetAttribute("settableOutput", "true");
	}
	if ((pin->GetFlags() & IO_LINEAR_INPUT) != 0)
	{
		pinXml->SetAttribute("linearInput", "true");
	}
	if ((pin->GetFlags() & IO_IGNORE_PATCH_CHANGE) != 0)
	{
		pinXml->SetAttribute("ignorePatchChange", "true");
	}
	if ((pin->GetFlags() & IO_AUTODUPLICATE) != 0)
	{
		pinXml->SetAttribute("autoDuplicate", "true");
	}
	if ((pin->GetFlags() & IO_CONTAINER_PLUG) != 0)
	{
		pinXml->SetAttribute("isContainerIoPlug", "true");
	}
	if ((pin->GetFlags() & IO_MINIMISED) != 0)
	{
		pinXml->SetAttribute("isMinimised", "true");
	}
	if ((pin->GetFlags() & IO_PAR_POLYPHONIC) != 0)
	{
		pinXml->SetAttribute("isPolyphonic", "true");
	}
	if ((pin->GetFlags() & IO_AUTOCONFIGURE_PARAMETER) != 0)
	{
		pinXml->SetAttribute("autoConfigureParameter", "true");
	}
	if ((pin->GetFlags() & IO_PARAMETER_SCREEN_ONLY) != 0)
	{
		pinXml->SetAttribute("noAutomation", "true");
	}

	if (pin->getParameterId() != -1)
	{
		pinXml->SetAttribute("parameterId", (int)pin->getParameterId());

		if (pin->getParameterFieldId() != FT_VALUE)
		{
			if (format == SAT_VST3)
			{
				pinXml->SetAttribute("parameterField", pin->getParameterFieldId()); // plain int for speed in compiled plugin.
			}
			else
			{
				pinXml->SetAttribute("parameterField", XmlStringFromParameterField(pin->getParameterFieldId()).c_str());
			}
		}
	}

	HostControls hostControlId = pin->getHostConnect();
	if (hostControlId != HC_NONE)
	{
		pinXml->SetAttribute("hostConnect", WStringToUtf8(GetHostControlName(hostControlId)).c_str());
		if (pin->getParameterFieldId() != FT_VALUE)
		{
			//			pinXml->SetAttribute("parameterField", XmlStringFromParameterField(pin->getParameterFieldId(0)));
			if (format == SAT_VST3)
			{
				pinXml->SetAttribute("parameterField", pin->getParameterFieldId()); // plain int for speed in compiled plugin.
			}
			else
			{
				pinXml->SetAttribute("parameterField", XmlStringFromParameterField(pin->getParameterFieldId()).c_str());
			}
		}
	}

	if (!pin->GetEnumList().empty())
	{
		pinXml->SetAttribute("metadata", WStringToUtf8(pin->GetEnumList()).c_str());
	}
}

// was Export()
tinyxml2::XMLElement* ExportModuleInfo(Module_Info* info, tinyxml2::XMLNode* element, ExportFormatType format, const std::string& overrideModuleId, const std::string& overrideModuleName)
{
	// Module_Info3_internal
	if (auto mi3i = dynamic_cast<Module_Info3_internal*>(info); mi3i)
	{
		// Because these modules are built-in to the executable, they register their XML in VST3 (unlike SEMs), so don't need to serialise XML.
		if (format == SAT_VST3)
			return nullptr;
	}

	auto doc = element->GetDocument();

	auto pluginXml = doc->NewElement("Plugin");
	element->LinkEndChild(pluginXml);

	auto uniqueId = WStringToUtf8(info->m_unique_id);
	if (!overrideModuleId.empty())
	{
		uniqueId = overrideModuleId;
	}

	auto moduleName = WStringToUtf8(GetName(info));

	if (!overrideModuleName.empty())
	{
		moduleName = overrideModuleName;
	}

	pluginXml->SetAttribute("id", uniqueId.c_str());

	if (SAT_SEM_CACHE == format || SAT_SYNTHEDIT_DOCUMENT == format)
	{
		if (const auto classTypeId = info->getClassType(); classTypeId != 0)
		{
			pluginXml->SetAttribute("class", classTypeId);
		}
	}

	pluginXml->SetAttribute("name", moduleName.c_str());

	if (info->isShellPlugin())
	{
		pluginXml->SetAttribute("shellPlugin", true);
	}

	// shortcut - save all module flags in one hit.
	if (format != SAT_VST3 && format != SAT_CODE_SKELETON)
	{
		pluginXml->SetAttribute("flags", info->flags);
	}

	// .. except in code-skeleton, where we want the flags spelled out.
	if ((info->flags & CF_ALWAYS_EXPORT) != 0 && format == SAT_CODE_SKELETON)
	{
		pluginXml->SetAttribute("alwaysExport", true);
	}

	if (format != SAT_VST3)
	{
		const auto category = WStringToUtf8(GetGroupName(info));
		pluginXml->SetAttribute("category", category.c_str());

		// "Debug" is absent from Release builds, so a tool must not hand one to
		// somebody as an ordinary result -- it would build a patch the user
		// cannot even open. It still matches a name search and reads as a good
		// hit: the Debug module "Slider2" shadows the Controls PREFAB of the
		// same name that users are actually told to use.
		//
		// NOT "Old". Those modules ship and work; they are merely superseded by
		// a current module of the same name. Flagging them here would make
		// callers skip modules that are legitimately available.
		if (category == "Debug" || category.rfind("Debug/", 0) == 0)
		{
			pluginXml->SetAttribute("deprecated", true);
		}
		if (!info->GetHelpUrl().empty())
		{
			auto helpfile = WStringToUtf8(info->GetHelpUrl());
			if (!overrideModuleId.empty())
			{
				helpfile = overrideModuleId + ".html";
			}
			pluginXml->SetAttribute("helpUrl", helpfile.c_str());
		}
	}
	// pluginXml->SetAttribute( "vendor", WStringToUtf8( ?? ));


	// Parameters.
	if (!info->m_parameters.empty())
	{
		auto parametersXml = doc->NewElement("Parameters");
		pluginXml->LinkEndChild(parametersXml);

		for (auto it = info->m_parameters.begin(); it != info->m_parameters.end(); ++it)
		{
			parameter_description* param = (*it).second;

			auto parameterXml = doc->NewElement("Parameter");
			parametersXml->LinkEndChild(parameterXml);
			parameterXml->SetAttribute("id", param->id);
			parameterXml->SetAttribute("datatype", XmlStringFromDatatype((int)param->datatype).c_str());
			parameterXml->SetAttribute("name", WStringToUtf8(param->name).c_str());
			if (!param->metaData.empty())
				parameterXml->SetAttribute("metadata", WStringToUtf8(param->metaData).c_str());
			if (!param->defaultValue.empty())
				parameterXml->SetAttribute("default", WStringToUtf8(param->defaultValue).c_str());

			int controllerType = param->automation >> 24;
			if (controllerType != ControllerType::None)
			{
				parameterXml->SetAttribute("automation", XmlStringFromController(controllerType).c_str());
			}
			//ar << param->flags;
			if ((param->flags & IO_PAR_PRIVATE) != 0)
			{
				parameterXml->SetAttribute("private", "true");
			}
			if ((param->flags & IO_IGNORE_PATCH_CHANGE) != 0)
			{
				parameterXml->SetAttribute("ignorePatchChange", "true");
			}
			if ((param->flags & IO_FILENAME) != 0)
			{
				parameterXml->SetAttribute("isFilename", "true");
			}
			if ((param->flags & IO_FILENAME_WRITABLE) != 0)
			{
				parameterXml->SetAttribute("isFilenameWritable", "true");
			}
			if ((param->flags & IO_PAR_POLYPHONIC) != 0)
			{
				parameterXml->SetAttribute("isPolyphonic", "true");
			}
			if ((param->flags & IO_PAR_POLYPHONIC_GATE) != 0)
			{
				parameterXml->SetAttribute("isPolyphonicGate", "true");
			}
			if ((param->flags & IO_PARAMETER_PERSISTANT) == 0)
			{
				parameterXml->SetAttribute("persistant", "false");
			}
		}
	}

	bool hasOldGuiPlugs = false;
	bool hasDspPlugs = false;
	for (auto it = info->plugs.begin(); it != info->plugs.end(); ++it)
	{
		auto pin = (*it).second;
		if (pin->isUiPlug())
		{
			hasOldGuiPlugs = true;
		}

		if (!pin->isUiPlug())
		{
			hasDspPlugs = true;
		}
	}

	// DSP plugs.
	if (hasDspPlugs || info->m_dsp_registered)
	{
		auto DspXml = doc->NewElement("Audio");
		pluginXml->LinkEndChild(DspXml);

		// Round-trip the module's declared latency, else it is silently lost whenever
		// module info comes from the SEM cache / an exported document rather than a
		// fresh scan of the module's own XML.
		if (info->latency != 0)
		{
			DspXml->SetAttribute("latency", info->latency);
		}

		int id = 0;
		for (auto it = info->plugs.begin(); it != info->plugs.end(); ++it)
		{
			auto pin = (*it).second;

			if (!pin->isUiPlug())
			{
				SaveModuleInfoPinXml(pin, format, DspXml, id);
				++id;
			}
		}
	}

	{
		// GUI plugs.
		tinyxml2::XMLElement* DspXml = 0;
		if (info->scanned_xml_gui || hasOldGuiPlugs || !info->gui_plugs.empty())
		{
			DspXml = doc->NewElement("GUI");
			pluginXml->LinkEndChild(DspXml);

			// graphicsApi.
			const char* graphicsApi = 0;
			switch (info->getWindowType())
			{
			case MP_WINDOW_TYPE_WPF:
				graphicsApi = "WPF";
				break;
			case MP_WINDOW_TYPE_WPF_INTERNAL:
				graphicsApi = "WPF-internal";
				break;
			case MP_WINDOW_TYPE_COMPOSITED:
				graphicsApi = "composited";
				break;
			case MP_WINDOW_TYPE_HWND:
				graphicsApi = "HWND";
				break;
			case MP_WINDOW_TYPE_XP:
				graphicsApi = "GmpiGui";
				break;
			default:
				graphicsApi = 0; // or "none";
			}

			if (graphicsApi)
			{
				if (format == SAT_CODE_SKELETON)
					graphicsApi = "GmpiUi";

				DspXml->SetAttribute("graphicsApi", graphicsApi);
			}

			// Default for these is 'true', so only set if false.
			if ((info->flags & CF_PANEL_VIEW) == 0 && !hasOldGuiPlugs) // SDK2 GUI modules only spawn on structure. However when upgrading to SDK3 they need to appear on GUI so ignore this flag.
			{
				DspXml->SetAttribute("DisplayOnPanel", "false");
			}
			if ((info->flags & CF_STRUCTURE_VIEW) == 0)
			{
				DspXml->SetAttribute("DisplayOnStructure", "false");
			}
		}

		if (hasOldGuiPlugs)
		{
			int id = 0;
			for (auto it = info->plugs.begin(); it != info->plugs.end(); ++it)
			{
				auto pin = (*it).second;
				if (pin->isUiPlug())
				{
					SaveModuleInfoPinXml(pin, format, DspXml, id);
					++id;
				}
			}
		}

		{
			int id = 0;
			for (auto it = info->gui_plugs.begin(); it != info->gui_plugs.end(); ++it)
			{
				auto pin = (*it).second;

				SaveModuleInfoPinXml(pin, format, DspXml, id);
				++id;
			}
		}
	}

	// Controller pins
	if (!info->controller_plugs.empty())
	{
		auto DspXml = doc->NewElement("Controller");
		pluginXml->LinkEndChild(DspXml);
		int id = 0;
		for (auto& it : info->controller_plugs)
		{
			auto pin = it.second;

			SaveModuleInfoPinXml(pin, format, DspXml, id);
			++id;
		}
	}

	if (auto mi3b = dynamic_cast<Module_Info3_base*>(info); mi3b)
	{
		for (auto& fn : Module_Info3_base::flagNames)
		{
			if (mi3b->ug_flags & fn.readFlag)
			{
				pluginXml->SetAttribute(fn.name, "true");
			}
		}

		// Module_Info3
		if (auto mi3 = dynamic_cast<Module_Info3*>(info); mi3)
		{
			if (CSynthEditDocBase::serializingMode == SERT_SEM_CACHE)
			{
				// "file" is the plugin path for this platform. So on mac its the mac bundle, on windows the dll.
				pluginXml->SetAttribute("file", mi3->holder.getPluginPath().string().c_str());

				// On windows we may also need the mac SEM path (which might be in a different folder).
#ifdef _WIN32
				if (!mi3->macSemBundlePath.empty())
				{
					pluginXml->SetAttribute("macSemBundlePath", WStringToUtf8(mi3->macSemBundlePath.c_str()).c_str());
				}
#endif
			}

			// Just for modules imbedded with VST3 plugins.
			if (format == SAT_VST3)
			{
				pluginXml->SetAttribute("imbeddedFilename", WStringToUtf8(StripPath(mi3->Filename())).c_str());
			}
		}
	}

	// Round-trip the patch points, for the same reason as `latency` above: they
	// are populated only by ScanXml on the module's own XML, so without this
	// they are silently lost whenever module info comes from the SEM cache or an
	// exported document rather than a fresh scan.
	//
	// The symptom is specific and baffling: a saved project reloads with one end
	// of every patch cable at the origin, because ModuleView::getConnectionPoint
	// finds no patchpoint matching the pin and returns a default-constructed
	// Point. Internal modules (SE Patch Point in/out) hide the bug — they
	// re-register from their literal XML on every startup, so their patch points
	// are always repopulated. Only an external .sem/.gmpi module can hit it.
	if (!info->patchPoints.empty())
	{
		auto patchPointsXml = doc->NewElement("PatchPoints");
		pluginXml->LinkEndChild(patchPointsXml);

		for (const auto& pp : info->patchPoints)
		{
			auto patchPointXml = doc->NewElement("PatchPoint");
			patchPointsXml->LinkEndChild(patchPointXml);

			patchPointXml->SetAttribute("pinId", pp.dspPin);

			// "x,y" — the format Module_Info::ScanXml parses back.
			const std::string centre = std::to_string(pp.x) + "," + std::to_string(pp.y);
			patchPointXml->SetAttribute("center", centre.c_str());

			patchPointXml->SetAttribute("radius", pp.radius);
		}
	}

	return pluginXml;
}

void ExportModuleInfo(tinyxml2::XMLNode* documentE, ExportFormatType fileType)
{
	if (CSynthEditDocBase::serializingMode != SERT_UNDO_SYSTEM)
	{
		auto factory = ModuleFactory();

		auto doc = documentE->GetDocument();
		auto databaseX = doc->NewElement("PluginList");
		documentE->LinkEndChild(databaseX);

		for (auto& it : factory->module_list)
		{
			if ((CSynthEditDocBase::serializingMode == SERT_SEM_CACHE && it.second->isDllAvailable()) || it.second->getSerialiseFlag())
			{
				ExportModuleInfo(it.second, databaseX, fileType);
			}
		}

		// Prefabs. These are not factory modules -- they are .syntheditprefab
		// files under the user's Prefabs folder -- so they never appeared in
		// this listing, which made them invisible to anything reading it. That
		// matters because the common controls users are told to reach for
		// (Slider2, Knob2, Switch, LED, Peak Meter) are prefabs, and a search
		// by name would instead surface a deprecated Debug module of the same
		// name and look like a hit.
		//
		// `id` is emitted in the exact form AddModule expects, so a caller can
		// paste it straight back without knowing the "*P=" convention.
		if (!factory->PrefabFileNames.empty())
		{
			auto prefabsX = doc->NewElement("PrefabList");
			documentE->LinkEndChild(prefabsX);

			for (const auto& relativePath : factory->PrefabFileNames)
			{
				const auto utf8 = WStringToUtf8(relativePath);
				auto prefabX = doc->NewElement("Prefab");

				// Display name: leaf, minus extension. What the browser shows.
				auto leaf = utf8;
				if (const auto slash = leaf.find_last_of("/\\"); slash != std::string::npos)
					leaf = leaf.substr(slash + 1);
				if (const auto dot = leaf.find_last_of('.'); dot != std::string::npos)
					leaf = leaf.substr(0, dot);

				// Category: the sub-folder it was scanned from, if any.
				std::string category;
				if (const auto slash = utf8.find_last_of("/\\"); slash != std::string::npos)
					category = utf8.substr(0, slash);

				prefabX->SetAttribute("id", ("*P=" + utf8).c_str());
				prefabX->SetAttribute("name", leaf.c_str());
				if (!category.empty())
					prefabX->SetAttribute("category", category.c_str());
				prefabX->SetAttribute("file", utf8.c_str());
				prefabsX->LinkEndChild(prefabX);
			}
		}
	}
}

void FlagHostControls(Module_Info* info, bool* flaggedHostControls)
{
	for (auto it : info->gui_plugs)
	{
		auto hc = it.second->getHostConnect();
		if (hc != HC_NONE)
		{
			flaggedHostControls[hc] = true;
		}
	}
	for (auto it : info->plugs)
	{
		auto hc = it.second->getHostConnect();
		if (hc != HC_NONE)
		{
			flaggedHostControls[hc] = true;
		}
	}
	for (auto it : info->controller_plugs)
	{
		auto hc = it.second->getHostConnect();
		if (hc != HC_NONE)
		{
			flaggedHostControls[hc] = true;
		}
	}
}

// when loading a project, the module info can be different.
// provide the original module info.
Module_Info* GetByIdSerializing(const std::wstring& p_id)
{
	auto it = m_in_use_old_module_list.find(p_id);
	if (it != m_in_use_old_module_list.end())
		return it->second.get();

	return CModuleFactory::Instance()->GetById(p_id);
}


#if 0 // defined( SE_ED IT_SUPPORT )
void CompareXml(TiXmlNode* original, TiXmlNode* copy, std::string indent)
{
	TiXmlElement* originalElement = original->ToElement();
	TiXmlElement* copyElement = copy->ToElement();
	for (TiXmlAttribute* attribute = originalElement->FirstAttribute(); attribute; attribute = attribute->Next())
	{
		string originalAtributeValue = attribute->NameTStr();
		_RPT3(_CRT_WARN, "%s%s \"%s\"", indent.c_str(), originalAtributeValue.c_str(), attribute->Value());
		string copyAtributeValue;
		auto r = copyElement->QueryValueAttribute(originalAtributeValue, &copyAtributeValue);
		if (r == 0)
		{
			if (copyAtributeValue == attribute->ValueStr())
			{
				_RPT0(_CRT_WARN, " *\n");
			}
			else
			{
				_RPT1(_CRT_WARN, ">\"%s\" DIFFERENT!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n", copyAtributeValue.c_str());
				//				assert( copyAtributeValue == "1" && attribute->ValueStr() == "1.0" ); // these are equivalent.
			}
		}
		else
		{
			_RPT0(_CRT_WARN, " FAIL TO FIND!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
			assert(originalAtributeValue == "vendor"); // legal, but not supported.
		}
	}

	for (auto child = original->FirstChild(); child; child = child->NextSibling())
	{
		if (child->Type() == TiXmlNode::NodeType::TINYXML_COMMENT)
		{
			continue;
		}

		TiXmlElement* originalChild = child->ToElement();

		_RPT2(_CRT_WARN, "%s%s ", indent.c_str(), originalChild->Value());

		TiXmlNode* copyChild = 0;

		// Pins have an "id" to identify them.
		string originalId;
		auto r = originalChild->QueryValueAttribute("id", &originalId);
		if (r != 0)
		{
			copyChild = copy->FirstChild(originalChild->Value());
		}
		else
		{
			for (copyChild = copy->FirstChild(originalChild->Value()); copyChild; copyChild = copyChild->NextSibling())
			{
				string copyId;
				TiXmlElement* copyChildElement = copyChild->ToElement();
				r = copyChildElement->QueryValueAttribute("id", &copyId);
				if (r == 0 && originalId == copyId)
				{
					break;
				}
			}
		}

		if (copyChild)
		{
			_RPT0(_CRT_WARN, " *\n");
			CompareXml(originalChild, copyChild, indent + "   ");
		}
		else
		{
			_RPT0(_CRT_WARN, " FAIL TO FIND!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
			assert(false);
		}
	}
}

#if 0

TiXmlElement* Module_Info3_base::ExportXml(TiXmlElement* element, ExportFormatType format, const string& overrideModuleId, const std::string& overrideModuleName)
{
	TiXmlElement* pluginXml = Module_Info::ExportXml(element, format, overrideModuleId, overrideModuleName);

	for (auto& fn : flagNames)
	{
		if (ug_flags & fn.readFlag)
		{
			pluginXml->SetAttribute(fn.name, "true");
		}
	}

	return pluginXml;
}
#endif

#endif

std::multimap<std::wstring, menuinfo> ExportModuleNames()
{
	std::multimap<std::wstring, menuinfo> m_menu_to_module_map;

	menuinfo mm{};

	for (auto& it : CModuleFactory::Instance()->module_list)
	{
		Module_Info* u = it.second;

		// lookup list is created only on application start, later after loading project with missing modules
		// extra modules will appear in database, need to completely skip those, else insert menu ends up off-by-one.

		if (u->isDllAvailable())
		{
			// Get the 'group' of UG types this belongs in
			mm.name = GetName(u);
			mm.group = GetGroupName(u);
			replacein(mm.group, L"/", L"\\"); // ensure all slashes consistant back-slashes.
			mm.unique_id = u->UniqueId();


#if defined( _DEBUG )
			//		_RPTW3(_CRT_WARN, L">> '%s' '%s.%s'", mm.unique_id, mm.group, mm.name);
			if (mm.name.empty())
			{
				mm.name = mm.unique_id;
			}
#endif
			std::wstring sortGroup = mm.group;

			if (sortGroup.empty())
			{
				sortGroup = L"AAAAAAAAA"; // "Container" etc sort first, not last.
			}

			std::wstring key = sortGroup + L"\\" + mm.name;
			//key.MakeLower();
			transform(key.begin(), key.end(), key.begin(), towlower);
			std::wstring keystring(key);

			//_RPTW0(_CRT_WARN, L"  *IN*");
			// Determine module visible or not on menu.
#if !defined( _DEBUG )
			if (mm.group != L"Debug")
#endif
			{
				assert(!mm.group.empty() || !mm.name.empty());

				mm.flavor = 0;
				if (!u->gui_plugs.empty())
				{
					mm.flavor = 1;

					if ((u->GetFlags() & CF_PANEL_VIEW) != 0 && !u->gui_object_non_visible())
					{
						mm.flavor = 2;
					}
				}

				m_menu_to_module_map.insert(std::pair< std::wstring, menuinfo >(keystring, mm));
			}
		}

		//		_RPTW0(_CRT_WARN, L"\n");
	}

	// PREFABS
	{
		std::multimap<std::wstring, menuinfo> temp_prefabs;
		std::vector<menuinfo> new_style;

		for(auto& path : CModuleFactory::Instance()->PrefabFileNames)
		{
			mm.unique_id = L"*P=" + path;

			std::wstring FileName, GroupName;

			auto cleanpath = ReplaceUnderscores(StripExtension(path));
			auto seperator = cleanpath.find_last_of(L"/\\");

			if(seperator != std::wstring::npos)
			{
				mm.group = Left(cleanpath, seperator);
				mm.name = Right(cleanpath, cleanpath.size() - 1 - seperator);
			}
			else
			{
				mm.name = cleanpath;
				mm.group = L"Prefabs";
			}

			replacein(mm.group, L"/", L"\\"); // ensure all slashes consistant back-slashes (mirrors module path above).
			std::wstring key = mm.group + L"\\" + mm.name;
			// make sorting case-insensitive.
			transform(key.begin(), key.end(), key.begin(), towlower);
			std::wstring keystring(key);

			temp_prefabs.insert(std::pair< std::wstring, menuinfo >(keystring, mm));

			if(mm.group == L"Controls")
				mm.flavor = 1;

			if(mm.unique_id.find(L".seprefab") == std::wstring::npos)
				new_style.push_back(mm);
		}

		// avoid double-ups from upgraded .seprefabs with same name/group. Prioritize newer formats over .seprefab
		for(auto it = temp_prefabs.begin(); it != temp_prefabs.end(); )
		{
			auto& prefab = *it;
			bool erased = false;
			if(prefab.second.unique_id.find(L".seprefab") != std::wstring::npos)
			{
				for(auto& mm : new_style)
				{
					if(prefab.second.name == mm.name && prefab.second.group == mm.group)
					{
						it = temp_prefabs.erase(it);
						erased = true;
						break;
					}
				}
			}
			if(!erased)
				++it;
		}

		m_menu_to_module_map.insert(temp_prefabs.begin(), temp_prefabs.end());
	}

	return std::move(m_menu_to_module_map);
}

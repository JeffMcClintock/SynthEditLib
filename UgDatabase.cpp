// Virtual base class for all unit generators
// (Anything that creates or modifies sound)

#include <assert.h>
#include <sstream>
#include "./modules/se_sdk3/mp_api.h"
#include "./modules/shared/xp_dynamic_linking.h"
#include "UgDatabase.h"
#include "ug_base.h"
#include "./modules/se_sdk3/MpString.h"
#include "./modules/tinyXml2/tinyxml2.h"
#include "conversion.h"
#include "RawConversions.h"
#include "mfc_emulation.h"
#include "SafeMessageBox.h"


#include "Module_Info3_internal.h"
#include "IPluginGui.h"
#include "tinyxml/tinyxml.h"
//#include "version.h"
#include "HostControls.h"
#include "midi_defs.h"
#include "./modules/shared/xplatform.h"
#include "./modules/shared/xp_dynamic_linking.h"
#include "BundleInfo.h"
#include "platform.h"

// support for 'real' GMPI plugins
#include "GmpiApiCommon.h"
#include "Common.h"

#if SE_EXTERNAL_SEM_SUPPORT==1
	#include "Module_Info3.h"
#endif

// provide extensibility to add extra modules on a per-project basis.
// SE2JUCE Controller must implement this function
extern void initialise_synthedit_extra_modules(bool passFalse);

typedef std::pair <std::wstring, Module_Info*> string_mod_info_pair;

using namespace std;
using namespace gmpi_sdk;
using namespace gmpi_dynamic_linking;

std::mutex RegisterExternalPluginsXmlOnce_lock;

const IOFlags IO_flagNames[15] = {
	{ ""	, IO_POLYPHONIC_ACTIVE			},
	{ "ignorePatchChange"		, IO_IGNORE_PATCH_CHANGE	},
	{ "autoRename"				, IO_RENAME					},
	{ "autoDuplicate"			, IO_AUTODUPLICATE			},
	{ "isFilename"				, IO_FILENAME				},
	{ "settableOutput"			, IO_SETABLE_OUTPUT			},
//	{ ""						, IO_CUSTOMISABLE			}, // DEPRECATED
	{ "isAdderInputPin"			, IO_ADDER					},
	{ "private"					, IO_HIDE_PIN				},
//	{ ""	, IO_DISABLE_IF_POS								}, // replaced with IO_HIDE_PIN
//	{ ""	, IO_PRIVATE									}, // replaced with IO_HIDE_PIN
	{ "linearInput"				, IO_LINEAR_INPUT			},
//	{ ""	, IO_UI_COMMUNICATION							}, // now set by <GUI> tag not an attribute
	{ "noAutomation"			, IO_PARAMETER_SCREEN_ONLY	},
//	{ ""	, IO_PATCH_STORE								}, // set indirectly by parameterId attribute
//	{ ""	, IO_PAR_PRIVATE								}, // flag used only for parameters, not pins
	{ "isMinimised"				, IO_MINIMISED				},
	{ "isContainerIoPlug"		, IO_CONTAINER_PLUG			},
//	{ ""	, IO_OLD_STYLE_GUI_PLUG							}, // hangover from SDK2, probably not needed.
//	{ ""	, IO_HOST_CONTROL								}, // set indirectly by hostControl
	{ "isPolyphonic"			, IO_PAR_POLYPHONIC			},
//	{ ""	, IO_PAR_POLYPHONIC_GATE						}, // flag used only for parameters, not pins
//	{ ""	, IO_PARAMETER_PERSISTANT						}, // flag used only for parameters, not pins
	{ "autoConfigureParameter"	, IO_AUTOCONFIGURE_PARAMETER},
	{ "redrawOnChange"			, IO_REDRAW_ON_CHANGE		},
};

int32_t CModuleFactory::PinFlagsFromXml(const char* name, const char* value)
{
	if (!value || strcmp("true", value) != 0)
		return 0;
	
	for (auto& pf : IO_flagNames)
	{
		if (name && strcmp(pf.name, name) == 0)
		{
			return pf.flag;
		}
	}
	
	return 0;
}

CModuleFactory::CModuleFactory()
{
	initialise_synthedit_modules();
}

gmpi::IMpUnknown* CModuleFactory::Create( int32_t subType, const std::wstring& uniqueId )
{
	Module_Info* mi = GetById( uniqueId );
	if( mi )
	{
		return mi->Build( subType );
	}

	return 0;
}

// when loading projects with non-available modules, a fake module info is created, but don't want it on Insert menu.
bool Module_Info::isDllAvailable()
{
	return m_module_dll_available;
}

bool Module_Info::alwaysExport()
{
	return (flags & CF_ALWAYS_EXPORT) != 0;
}

bool Module_Info::getSerialiseFlag()
{
	return m_serialise_me; // moved to ClearSerialiseFlags(). || ((flags & CF_ALWAYS_EXPORT) != 0);
}

bool Module_Info::hasDspModule()
{
	// Note: SDK2 has DSP regardless of needed or not.
	if( ModuleTechnology() < MT_SDK3 )
	{
		// assume it has DSP if it has DSP pins.
		for( auto it = plugs.begin() ; it != plugs.end() ; ++it )
		{
			if( !(*it).second->isUiPlug() )
				return true;
		}
		return false;
	}
	return m_dsp_registered;
}

bool Module_Info::OnDemandLoad()
{
	return true;
}

bool Module_Info::gui_object_non_visible()
{
	// CF_NON_VISIBLE used to include MP_WINDOW_TYPE_COMPOSITED, MP_WINDOW_TYPE_HWND, MP_WINDOW_TYPE_VSTGUI
	// but these are deprecated anyhow
	return getWindowType() != MP_WINDOW_TYPE_XP;
//	return ( GetFlags() & CF_NON_VISIBLE) != 0;
}

Module_Info* CModuleFactory::FindOrCreateModuleInfo(const std::wstring& p_unique_id)
{
	auto it = module_list.find(p_unique_id);

	if (it == module_list.end())
	{
		Module_Info* mi = new Module_Info(p_unique_id);
		auto res = module_list.insert({ (mi->UniqueId()), mi });
		assert(res.second); // insert failed.
		return mi;
	}
	else
	{
		return (*it).second;
	}
}

// SDK3/ GMPI modules
Module_Info3_internal* CModuleFactory::FindOrCreateModuleInfo3(const std::wstring& p_unique_id)
{
	if (auto it = module_list.find(p_unique_id); it != module_list.end())
	{
		auto mi3 = dynamic_cast<Module_Info3_internal*>((*it).second);
		assert(mi3 && "wrong type of module");
		return mi3;
	}

	auto mi = new Module_Info3_internal(p_unique_id.c_str());
	auto res = module_list.insert({ (mi->UniqueId()), mi });
	assert(res.second); // insert failed.
	return mi;
}

// real GMPI factory registration
namespace gmpi
{
// register a plugin component with the factory
gmpi::ReturnCode RegisterPlugin(gmpi::api::PluginSubtype subType, const char* uniqueId, gmpi::CreatePluginPtr create)
{
	const auto uniqueIdW = Utf8ToWstring(uniqueId);
	return (gmpi::ReturnCode) ModuleFactory()->RegisterPlugin((int) subType, uniqueIdW.c_str(), (MP_CreateFunc2)create);
}
gmpi::ReturnCode RegisterPluginWithXml(gmpi::api::PluginSubtype subType, const char* xml, gmpi::CreatePluginPtr create)
{
	return (gmpi::ReturnCode) ModuleFactory()->RegisterPluginWithXml((int) subType, xml, (MP_CreateFunc2) create);
}
}

// register one or more *internal* plugins with the factory
int32_t RegisterPluginXml( const char* xmlFile )
{
	// waves puts all XML in project file. exception is "helper" modules not explicitly in patch.
	ModuleFactory()->RegisterPluginsXml( xmlFile );
	return gmpi::MP_OK;
}

// New generic object register.
int32_t RegisterPlugin_lib( int subType, const wchar_t* uniqueId, MP_CreateFunc2 create )
{
	return ModuleFactory()->RegisterPlugin( subType, uniqueId, create );
}

int32_t RegisterPluginWithXml_lib(int subType, const char* xml, MP_CreateFunc2 create)
{
	return ModuleFactory()->RegisterPluginWithXml(subType, xml, create);
}

// register an internal ug_base derived module.
namespace internalSdk
{
	bool RegisterPlugin(ug_base *(*ug_create)(), const char* xml)
	{
		return ModuleFactory()->RegisterModule( new Module_Info(ug_create, xml) );
	}
}

int32_t CModuleFactory::RegisterPlugin( int subType, const wchar_t* uniqueId, MP_CreateFunc2 create )
{
	auto mi3 = FindOrCreateModuleInfo3(uniqueId);
	return mi3->RegisterPluginConstructor( subType, create );
}

std::wstring parseModuleId(tinyxml2::XMLDocument& doc, const char* xml)
{
	doc.Parse(xml);

	if (!doc.Error())
	{
		auto pluginList = doc.FirstChildElement("PluginList");
		auto PluginElement = pluginList->FirstChildElement("Plugin");
		assert(PluginElement);
		return Utf8ToWstring(PluginElement->Attribute("id"));
	}

	return{};
}

int32_t CModuleFactory::RegisterPluginWithXml(int subType, const char* xml, MP_CreateFunc2 create)
{
	tinyxml2::XMLDocument doc;
	auto uniqueId = parseModuleId(doc, xml);

	if (uniqueId.empty())
	{
		std::wostringstream oss;
		oss << L"Module XML Error: [SynthEdit.exe]" << doc.ErrorName() << L"." << doc.Value();
		SafeMessagebox(0, oss.str().c_str(), L"", MB_OK | MB_ICONSTOP);
		return gmpi::MP_FAIL;
	}

	auto mi3 = FindOrCreateModuleInfo3(uniqueId);

	mi3->ScanXml(doc.FirstChildElement("PluginList")->FirstChildElement("Plugin")->ToElement());

	return mi3->RegisterPluginConstructor(subType, create);
}

// Register DSP section of module
// this may look unnesc, but results in smaller registration code per-module (module don't need to create new Module_Info() )
bool CModuleFactory::RegisterModule( module_description_internal& p_module_desc ) // obsolete
{
	//_RPT1(_CRT_WARN, "RegisterModule %s\n", p_module_desc.unique_id );
	FindOrCreateModuleInfo(Utf8ToWstring(p_module_desc.unique_id) )->Register(p_module_desc);
	return true;
}

// Register GUI section of module. SDK3 compatible
bool CModuleFactory::RegisterModule( get_module_properties_func mod_func, get_pin_properties_func pin_func)
{
	module_description mod;
	memset( &mod,0,sizeof(mod));
	mod_func(mod);
	//_RPT1(_CRT_WARN, "RegisterModule %s\n", mod.unique_id );
	FindOrCreateModuleInfo(Utf8ToWstring(mod.unique_id) )->Register(mod, pin_func);
	return true;
}

void Module_Info::Register( struct module_description& mod, get_pin_properties_func get_pin_properties) // GUI
{
	flags			|= mod.flags;
	m_unique_id		= Utf8ToWstring(mod.unique_id);
	// split name into group/name
	std::wstring s(mod.name);
	size_t separator = s.find_last_of( L'/' );

	if( separator != string::npos ) // split name/groupname
	{
		m_group_name = s.substr(0, separator );
		m_name = s.substr(separator + 1, s.size() - separator - 1 );
	}
	else
	{
		m_name = s;
	}

	assert( !m_gui_registered );

	int i = 0;
	pin_description pin;
	bool more;

	do
	{
		const wchar_t* emptyString = L"";
		memset( &pin,0,sizeof(pin));
		pin.default_value = pin.meta_data = pin.name = pin.notes = emptyString;
		more = get_pin_properties( i, pin );

		if( more )
		{
			InterfaceObject* iob = new_InterfaceObjectB( i, pin );
			iob->SetFlags( iob->GetFlags() | IO_UI_COMMUNICATION ); // set as GUI-Plug (saves every module adding this flag to every pin)
			gui_plugs.insert({ i,iob });
			++i;
		}
	}
	while(more);

	m_gui_registered = true;
}

// Register DSP section of module
bool CModuleFactory::RegisterModule(module_description_dsp& p_module_desc)
{
	FindOrCreateModuleInfo(Utf8ToWstring(p_module_desc.unique_id) )->Register(p_module_desc);
	return true;
}

// register a module (old SDK2 style GUI pins).
// would probly make more sense to have GUI and DSP objects as two things, not two sections of one thing
void Module_Info::Register(module_description_internal& p_module_desc)
{
	flags |= p_module_desc.flags;
	m_unique_id = Utf8ToWstring( p_module_desc.unique_id );
	dsp_create = p_module_desc.ug_create;

	if( p_module_desc.name_string_resource_id > -1 )
	{
		sid_name = p_module_desc.name_string_resource_id;
		sid_group = p_module_desc.category_string_resource_id;
	}

	m_loaded_into_database = true;

	// new way, dosen't require instansiating, replaces SetupPlugs()
	for( int i = 0 ; i < p_module_desc.pin_descriptions_count ; i++ )
	{
		InterfaceObject* io = new_InterfaceObjectB( i, p_module_desc.pin_descriptions[i] );
		plugs.insert({ i, io });

		// detect old-style GUI pins
		if( 0 != (p_module_desc.pin_descriptions[i].flags & IO_UI_COMMUNICATION) )
		{
			io->SetFlags( io->GetFlags() | IO_OLD_STYLE_GUI_PLUG );
		}

		if( io->isParameterPlug() )
		{
			assert( io->getParameterId() == -1 );
			io->setParameterId(0); // assuming my own modules use only one parameter max.

			if( io->getParameterFieldId() == FT_VALUE )
			{
				RegisterSdk2Parameter( io );
			}
		}
	}

	m_dsp_registered = true;
}

// new way
void Module_Info::Register(module_description_dsp& p_module_desc)
{
	assert( !m_dsp_registered );
	flags |= p_module_desc.flags | CF_STRUCTURE_VIEW; // structure view assumed
	m_unique_id = Utf8ToWstring( p_module_desc.unique_id );
	dsp_create = p_module_desc.ug_create;

	if( p_module_desc.name_string_resource_id > -1 )
	{
		sid_name = p_module_desc.name_string_resource_id;
		sid_group = p_module_desc.category_string_resource_id;
	}

	m_loaded_into_database = true;

	// new way, dosen't require instansiating, replaces SetupPlugs()
	for( int i = 0 ; i < p_module_desc.pin_descriptions_count ; i++ )
	{
		InterfaceObject* io = new_InterfaceObjectB( i, p_module_desc.pin_descriptions[i] );
		plugs.insert({ i, io } );

		if( (io->GetFlags() & IO_PATCH_STORE ) != 0 )
		{
			assert( io->getParameterId() == -1 );
			io->setParameterId(0); // assuming my own modules use only one parameter max.
			RegisterSdk2Parameter( io );
		}
	}

	m_dsp_registered = true;
}

// allow modules to register here without pulling in the entire UgDatabase or ModuleInfo header.
bool ModuleFactory_RegisterModule(const wchar_t* p_unique_id, int p_sid_name, int p_sid_group_name, class CDocOb* (*cug_create)(Module_Info*), class ug_base* (*ug_create)(), int p_flags)
{
	return CModuleFactory::Instance()->RegisterModule(new Module_Info(p_unique_id, p_sid_name, p_sid_group_name, cug_create, ug_create, p_flags));
}

bool ModuleFactory_RegisterModule(const wchar_t* p_unique_id, const wchar_t* name, const wchar_t* group_name, class CDocOb* (*cug_create)(Module_Info*), class ug_base* (*ug_create)(), int p_flags)
{
	return CModuleFactory::Instance()->RegisterModule(new Module_Info(p_unique_id, name, group_name, cug_create, ug_create, p_flags));
}

// using older macros - REGISTER_MODULE_1_BC
bool CModuleFactory::RegisterModule(Module_Info* p_module_info)
{
	// ignore obsolete ones
	if( p_module_info->UniqueId().empty() )
	{
		delete p_module_info;
		return false;
	}

	auto res = module_list.insert({ p_module_info->UniqueId(), p_module_info });

	if( res.second == false )
	{
		if( !p_module_info->UniqueId().empty() )
		{
			std::wostringstream oss;
			oss << L"RegisterModule Error. Module found twice. " << p_module_info->UniqueId();
			SafeMessagebox(0, oss.str().c_str(), L"", MB_OK|MB_ICONSTOP );
			assert(false);
		}

		delete p_module_info;
	}

	return true;
}

void Module_Info::setShellPlugin()
{
	flags |= CF_SHELL_PLUGIN;
}

bool Module_Info::isShellPlugin()
{
	return ( flags & CF_SHELL_PLUGIN ) != 0;
}

void CModuleFactory::RegisterPluginsXml( const char* xml_data )
{
	tinyxml2::XMLDocument doc;
	doc.Parse( xml_data );

	if ( doc.Error() )
	{
		std::wostringstream oss;
		oss << L"Module XML Error: [SynthEdit.exe]" << doc.ErrorName() << L"." <<  doc.Value();
		SafeMessagebox(0, oss.str().c_str(), L"", MB_OK|MB_ICONSTOP );

#if defined( _DEBUG )
		for( int i = doc.ErrorLineNum() - 5 ; i < doc.ErrorLineNum() + 5 ; ++i )
		{
			_RPT1(_CRT_WARN, "%c", xml_data[i] );
		}
		_RPT0(_CRT_WARN, "\n");
		assert(false);
#endif

		return;
	}

	auto pluginList = doc.FirstChildElement("PluginList");

	if(pluginList) // Check it is a plugin description (not some other XML file in UAP)
		RegisterPluginsXml(pluginList);
}

void CModuleFactory::RegisterExternalPluginsXmlOnce(TiXmlNode* /* pluginList */)
{
	std::lock_guard<std::mutex> guard(RegisterExternalPluginsXmlOnce_lock);

	// In VST3, can be called from either Controller, or Processor (whichever the DAW initializes first)
	// ensure only init once.
	if (initializedFromXml)
	{
		return;
	}
	initializedFromXml = true;

	{
		// On VST3 database is separate XML to make loading faster.
		auto bundleinfo = BundleInfo::instance();

		// Modules database.
		auto databaseXml = bundleinfo->getResource("database.se.xml");
		tinyxml2::XMLDocument doc;
		doc.Parse(databaseXml.c_str());

		if (doc.Error())
		{
			assert(false);
			return;
		}

		auto document_xml = doc.FirstChildElement("Document");
		RegisterPluginsXml(document_xml->FirstChildElement("PluginList"));
	}
}

void CModuleFactory::RegisterPluginsXml(tinyxml2::XMLElement* pluginList )
{
	// Walk all plugins.
	for( auto PluginElement = pluginList->FirstChildElement( "Plugin" ); PluginElement; PluginElement = PluginElement->NextSiblingElement( "Plugin" ) )
	{
		// check for existing
//		TiXmlElement* PluginElement = node->ToElement();
		wstring pluginId = Utf8ToWstring( PluginElement->Attribute("id") );
		Module_Info3_base* mi3 = 0;
		Module_Info* mi = ModuleFactory()->GetById( pluginId );

		if( mi )
		{
			mi3 = dynamic_cast<Module_Info3_internal*>( mi );

			if( mi3 == 0 )
			{
				assert( false && "Duplicate Module" );
				return;
			}
			mi3->ScanXml( PluginElement);
		}
		else
		{
			wstring imbeddedFilename = Utf8ToWstring( PluginElement->Attribute("imbeddedFilename") );
			if( imbeddedFilename.empty() )
			{
				// Internal module compiled with engine.
				mi3 = new Module_Info3_internal( pluginId.c_str() );
			}
			else
			{
                #if SE_EXTERNAL_SEM_SUPPORT==1
				// External module in VST3 folder.
					mi3 = new Module_Info3( imbeddedFilename );
                #else
					// * Check module source file is included in project.
					// * Check source contains 'SE_DECLARE_INIT_STATIC_FILE' macro.
					// * Check this file contains 'INIT_STATIC_FILE' macro.
					// if using JUCE GUI, it's OK not to register GUI modules because they are not supported.
					_RPT1(0, "Module not available: %S\n", pluginId.c_str());
#if defined( _DEBUG)
					failedGuiModules.push_back(pluginId);
#endif
					continue;
                #endif
			}

            mi3->ScanXml( PluginElement);

			auto res = module_list.insert( string_mod_info_pair( (mi3->UniqueId()), mi3) );
			assert( res.second ); // insert failed.
		}
	}
}

Module_Info::Module_Info(const std::wstring& p_unique_id) :
	flags(0)
	,m_unique_id(p_unique_id)
	,dsp_create(0)
	,sid_name(0)//-1)
	,sid_group(0)//-1)
	,m_loaded_into_database(true)
	,m_module_dll_available(true)
	, latency(0)
	, scanned_xml_dsp(false)
	, scanned_xml_gui(false)
	, scanned_xml_parameters(false)
{
	//_RPT0(_CRT_WARN, "create Module_Info\n" );
}

Module_Info::Module_Info(class ug_base *(*ug_create)(), const char* xml) :
	flags(0)
	, dsp_create(ug_create)
	, sid_name(0)
	, sid_group(0)
	, m_loaded_into_database(true)
	, m_module_dll_available(true)
	, latency(0)
	, scanned_xml_dsp(false)
	, scanned_xml_gui(false)
	, scanned_xml_parameters(false)
{
	tinyxml2::XMLDocument doc2;
	doc2.Parse(xml);

	if (doc2.Error())
	{
		std::wostringstream oss;
		oss << L"Module XML Error: [SynthEdit.exe]" << doc2.ErrorName() << L"." << doc2.Value();

		SafeMessagebox(0, oss.str().c_str(), L"", MB_OK | MB_ICONSTOP);
	}
	else
	{
		auto pluginList = doc2.FirstChildElement("PluginList");
		auto node = pluginList->FirstChildElement("Plugin");
		assert(node);
		auto PluginElement = node->ToElement();
		m_unique_id = Utf8ToWstring(PluginElement->Attribute("id"));

		ScanXml(PluginElement);
	}
}

Module_Info::Module_Info() :
	m_loaded_into_database(false)
	,m_module_dll_available(true)
	,dsp_create(0)
	,sid_name(0)
	,sid_group(0)
	,flags(0)
	, latency(0)
	, scanned_xml_dsp(false)
	, scanned_xml_gui(false)
	, scanned_xml_parameters(false)
{}; // serialisation only

// REGISTER_MODULE_3_BC
Module_Info::Module_Info(const wchar_t* p_unique_id, int p_sid_name, int p_sid_group_name, CDocOb *( *cug_create )( Module_Info* ), ug_base *( *ug_create )( ), int p_flags) :
	flags(p_flags)
	,m_unique_id(p_unique_id)
	,dsp_create(ug_create)
	,sid_name(p_sid_name)
	,sid_group(p_sid_group_name)
	,m_loaded_into_database(true)
	,m_module_dll_available(true)
	, latency(0)
	, scanned_xml_dsp(false)
	, scanned_xml_gui(false)
	, scanned_xml_parameters(false)
{
	//_RPT0(_CRT_WARN, "create Module_Info\n" );
	flags |= CF_OLD_STYLE_LISTINTERFACE;
	/*testing
		if( p_type == 126 )
		{
			for(int i = 0; i < name.size() ; i++ )
			{
				wchar_t c = name[i];
				_RPT2(_CRT_WARN, "%c %d\n", c, (int)c );
			}
		}
		*/
	SetupPlugs();
}

Module_Info::Module_Info(const wchar_t* p_unique_id, const wchar_t* name, const wchar_t* group_name, CDocOb *( *cug_create )( Module_Info* ), ug_base *( *ug_create )( ), int p_flags) :
flags(p_flags)
, m_unique_id(p_unique_id)
, dsp_create(ug_create)
, sid_name(0)
, sid_group(0)
, m_name(name)
, m_group_name(group_name)
, m_loaded_into_database(true)
, m_module_dll_available(true)
, latency(0)
, scanned_xml_dsp(false)
, scanned_xml_gui(false)
, scanned_xml_parameters(false)
{
	flags |= CF_OLD_STYLE_LISTINTERFACE;

	SetupPlugs();
}

Module_Info::~Module_Info()
{
	//	_RPT0(_CRT_WARN, "delete ~Module_Info\n" );
	ClearPlugs();
}

std::wstring Module_Info::GetHelpUrl()
{
	return helpUrl_;
}


void Module_Info::ClearPlugs()
{
	for( auto& it : plugs )
	{
		delete it.second;
	}

	for (auto& it : gui_plugs)
	{
		delete it.second;
	}

	for (auto& it : controller_plugs)
	{
		delete it.second;
	}

	for (auto& it : m_parameters)
	{
		delete it.second;
	}

	plugs.clear();
	gui_plugs.clear();
	controller_plugs.clear();
	m_parameters.clear();
}

ug_base* Module_Info::BuildSynthOb()
{
	if( dsp_create == NULL )
		return NULL; // trying to build an object that can't be (display-only object)

	ug_base* module = dsp_create();
	module->moduleType = this;
	module->latencySamples = latency;
	return module;
}

// Meyer's singleton
CModuleFactory* CModuleFactory::Instance()
{
	static CModuleFactory obj;
	return &obj;
}

CModuleFactory::~CModuleFactory()
{
	for( auto& it : module_list)
	{
		delete it.second;
	}

	module_list.clear();

	// Check for modules which we forgot to static init in UgDatabase.cpp
#if defined(_DEBUG)

	for (auto& s : staticInitCheck2)
	{
		if (s.find("SE ") == 0 || s.find("SE:") == 0)
		{
			s = s.substr(3);
		}
		std::replace(s.begin(), s.end(), ' ', '_');
	}

	// delete even elements from the vector
	staticInitCheck2.erase(std::remove_if(staticInitCheck2.begin(), staticInitCheck2.end(), [this](const std::string& x) {
		return std::find(staticInitCheck.begin(), staticInitCheck.end(), x) != staticInitCheck.end();
		}), staticInitCheck2.end());

	for (const auto s : staticInitCheck2)
	{
		_RPTN(0, "INIT_STATIC_FILE(%s);\n", s.c_str());
	}
	assert(staticInitCheck2.empty()); // you have omitted a INIT_STATIC_FILE in UgDatabase.cpp for this module

#endif
}

void CModuleFactory::ClearModuleDb( const std::wstring& p_extension )
{
	for( auto it = PrefabFileNames.begin() ;  it != PrefabFileNames.end() ; )
	{
		std::wstring extension = GetExtension( *it );

		if( extension.compare( p_extension ) == 0 )
		{
			it = PrefabFileNames.erase(it);
		}
		else
		{
			++it;
		}
	}
}

// On save, relevant module infos are flagged for saving
void CModuleFactory::ClearSerialiseFlags(bool isExportingPlugin)
{
	for( auto& m : module_list)
	{
		if (isExportingPlugin && m.second->alwaysExport())
		{
			assert(m.second->isDllAvailable()); // hmm, essential but not available?
			m.second->SetSerialiseFlag();
		}
		else
		{
			m.second->ClearSerialiseFlag();
		}
	}
}

void Module_Info::SetupPlugs()
{
	// old way, requires instansiating (new way done in contructor)
	// instansiate ug
	SetupPlugs_pt2( BuildSynthOb() );
	m_dsp_registered = true;
}

void Module_Info::SetupPlugs_pt2(ug_base* ug)
{
	if( ug != NULL )	// ignore display-only objects
	{
		InterfaceObjectArray temp;
		ug->ListInterface2( temp );	// and query it's interface
		delete ug;

		// set plug IDs
		for( int id = 0 ; id < (int) temp.size() ; id++ )
		{
			InterfaceObject* io = temp[id];
			io->setId(id);
			// care not to store pointer to variable belonging to deleted ug object.
			io->clearVariableAddress();

			if (io->isUiPlug())
			{
				gui_plugs.insert({ id, io });
			}

			if (!io->isUiPlug())
			{
				plugs.insert({ id, io });
			}

			// flag old-style gui connectors (require connecting in-Document)
			if( io->isUiPlug() )
			{
				io->SetFlags( io->GetFlags() | IO_OLD_STYLE_GUI_PLUG );
				// Fix for buggy "SL ExpScaler" "Param plug wrong direction"
			}

			/*
			#if defined( _DEBUG )
						if( !io->CheckEnumOnConnection(0) && io->GetDatatype() == DT_ENUM )
			{
				_RPTW1(_CRT_WARN, L"CheckEnumOnConnection FALSE %s.\n", this->GetName() );
			}
			#endif
			*/
			// parameters stored seperate.
			if( io->isParameterPlug() )
			{
				if( io->getParameterId() < 0 )
					io->setParameterId(0); // assuming my own modules use only one parameter max.

				if( io->getParameterFieldId() == FT_VALUE )
				{
					if( io->isUiPlug() && io->GetDirection() == DR_IN )
					{
						_RPT0(_CRT_WARN, "SDK2 module has PATCH-STORE on DR_IN pin!!.\n" );
						/*, no causes DUAL pins to not appear in DSP pins.
											int f = io->GetFlags();
											// note: clearing PS flag causes module to instansiate with non-sequential parameters.
											// i.e. 'KDL Volts2GuiFloat' has param 0,1, and 3 (but not 2).
											CLEAR_BITS( f, IO_UI_DUAL_FLAG|IO_PATCH_STORE);
											io->SetFlags( f );
						*/
					}
					else
					{
						RegisterSdk2Parameter( io );
					}
				}
			}
		}
	}
}

void Module_Info::RegisterSdk2Parameter( InterfaceObject* io )
{
	parameter_description* pd = GetOrCreateParameter( io->getParameterId() );
	pd->name = io->GetName();
	pd->flags = IO_PARAMETER_PERSISTANT | (io->GetFlags() & (IO_PAR_PRIVATE|IO_PAR_POLYPHONIC|IO_PAR_POLYPHONIC_GATE|IO_HOST_CONTROL|IO_IGNORE_PATCH_CHANGE));
	pd->datatype = io->GetDatatype();
	//	pd->automation = io->automation();
	pd->defaultValue = io->GetDefaultVal();

	if( pd->datatype == DT_ENUM )
	{
		pd->metaData = ( io->GetEnumList() );
	}
}

parameter_description* Module_Info::GetOrCreateParameter(int parameterId)
{
	auto it = m_parameters.find( parameterId );

	if( it != m_parameters.end() )
	{
		return (*it).second;
	}

	parameter_description* param = new parameter_description();
	param->id = parameterId;

	bool res = m_parameters.insert({ param->id, param }).second;
	assert( res == true );
	return param;
}

InterfaceObject* Module_Info::getPinDescriptionByPosition(int p_id)
{
	int i = 0;

	for( auto it = plugs.begin() ; it != plugs.end() ; ++it )
	{
		if( i++ == p_id )
		{
			return (*it).second;
		}
	}

	return 0;
}

InterfaceObject* Module_Info::getPinDescriptionById(int p_id)
{
	auto it = plugs.find(p_id);

	if( it != plugs.end() )
	{
		return (*it).second;
	}

	// ok to return 0 with dynamic plugs. assert( m_dsp_registered && "DSP class not registered (included in se_vst project?" );
	return 0;
}

InterfaceObject* Module_Info::getGuiPinDescriptionByPosition(int p_index)
{
	int i = 0;

	for( auto it = gui_plugs.begin() ; it != gui_plugs.end() ; ++it )
	{
		if( i++ == p_index )
		{
			return (*it).second;
		}
	}

	return 0;
}

InterfaceObject* Module_Info::getGuiPinDescriptionById(int p_id)
{
	auto it = gui_plugs.find(p_id);

	if( it != gui_plugs.end() )
	{
		return (*it).second;
	}

	return 0;
}

parameter_description* Module_Info::getParameterById(int p_id)
{
	auto it = m_parameters.find(p_id);

	if( it != m_parameters.end() )
	{
		return (*it).second;
	}

	return 0;
}

parameter_description* Module_Info::getParameterByPosition(int index)
{
	int i = 0;

	for(auto it = m_parameters.begin() ; it != m_parameters.end() ; ++it )
	{
		if( i++ == index )
		{
			return (*it).second;
		}
	}

	return 0;
}

Module_Info* CModuleFactory::GetById(const std::wstring& p_id)
{
	auto it = module_list.find( p_id );

	if( it != module_list.end() )
	{
		return ( *it ).second;
	}
	else
	{
#ifdef _DEBUG
		if( Left(p_id,2).compare( L"BI" ) == 0 )
		{
			assert(false && "old-style no longer supported, use SE1.1 to upgrade project" );
		}
#endif
		//_RPTW1(_CRT_WARN, L"CModuleFactory::GetById(%s) can't Find. (included file?)\n", p_id.c_str() );
		return 0;
	}
}

#if defined( _DEBUG)
std::string CModuleFactory::GetFailedGuiModules()
{
	std::string ret;
	for (auto& it : module_list)
	{
		if (it.second->load_failed_gui)
		{
			ret += WStringToUtf8(it.second->UniqueId()) + "\n";
		}
	}

	sort(failedGuiModules.begin(), failedGuiModules.end());
	failedGuiModules.erase(unique(failedGuiModules.begin(), failedGuiModules.end()), failedGuiModules.end());

	for (auto& s : failedGuiModules)
	{
		ret += WStringToUtf8(s) + "\n";
	}

	return ret;
}
#endif

#if defined(_DEBUG)
bool CModuleFactory::debugInitCheck(const char* modulename)
{
	staticInitCheck2.push_back(modulename);
	return false;
}
#endif

// When running in a static library, this mechanism forces the linker to inlude all self-registering modules.
// without this the linker will decide that they can be discarded, since they are not explicity referenced elsewhere.
// This can also be run harmlessly in builds where it is not needed.
// see also SE_DECLARE_INIT_STATIC_FILE
// see also ExtraModules.cpp

#if defined(_DEBUG)
// has extra debugging check
#define INIT_STATIC_FILE(filename) void se_static_library_init_##filename(); se_static_library_init_##filename(); staticInitCheck.push_back( #filename );
#else
#define INIT_STATIC_FILE(filename) void se_static_library_init_##filename(); se_static_library_init_##filename();
#endif

void CModuleFactory::initialise_synthedit_modules(bool passFalse)
{
#ifndef _DEBUG
	// NOTE: We don't have to actually call these functions (they do nothing), only *reference* them to
	// cause them to be linked into plugin such that static objects get initialised.
    if(!passFalse)
        return;
#endif

#if GMPI_IS_PLATFORM_JUCE==1
	INIT_STATIC_FILE(ADSR);
	INIT_STATIC_FILE(BlobToBlob2)
	INIT_STATIC_FILE(BpmClock3);
	INIT_STATIC_FILE(BPMTempo);
	INIT_STATIC_FILE(ButterworthHP);
    INIT_STATIC_FILE(ButterworthBP2);
	INIT_STATIC_FILE(Converters);
	INIT_STATIC_FILE(FreqAnalyser2);
	INIT_STATIC_FILE(FreqAnalyser3);
	INIT_STATIC_FILE(IdeLogger);
	INIT_STATIC_FILE(ImpulseResponse);
	INIT_STATIC_FILE(ImpulseResponse2);
	INIT_STATIC_FILE(Inverter);
	INIT_STATIC_FILE(MIDItoGate);
	INIT_STATIC_FILE(NoteExpression);
	INIT_STATIC_FILE(OscillatorNaive);
	INIT_STATIC_FILE(PatchMemoryBool);
	INIT_STATIC_FILE(PatchMemoryBool);
	INIT_STATIC_FILE(PatchMemoryFloat);
	INIT_STATIC_FILE(PatchMemoryInt);
	INIT_STATIC_FILE(PatchMemoryList3);
	INIT_STATIC_FILE(PatchMemoryText);
	INIT_STATIC_FILE(Slider);
	INIT_STATIC_FILE(Scope3XP)
	INIT_STATIC_FILE(SVFilter4)
	INIT_STATIC_FILE(SoftDistortion);
	INIT_STATIC_FILE(Switch);
	INIT_STATIC_FILE(SilenceGate);
	INIT_STATIC_FILE(UnitConverter);
	INIT_STATIC_FILE(VoltsToMIDICC);
	INIT_STATIC_FILE(Waveshaper3Xp);
	INIT_STATIC_FILE(Waveshapers);
	INIT_STATIC_FILE(UserSettingText); // not generated automatically at present
	INIT_STATIC_FILE(UserSettingText_Controller);
	INIT_STATIC_FILE(ImpulseResponse);

//	INIT_STATIC_FILE(Dynamics);
//	INIT_STATIC_FILE(VoltMeter);
//	INIT_STATIC_FILE(SignalLogger);

	// when the UI is defined in JUCE, not SynthEdit, we don't include GUI modules
#if SE_GRAPHICS_SUPPORT
	INIT_STATIC_FILE(UserSettingText_Gui);
	INIT_STATIC_FILE(Converters_GUI)
	INIT_STATIC_FILE(FloatScaler2Gui);
	INIT_STATIC_FILE(PatchMemoryFloat_Gui)
	INIT_STATIC_FILE(PatchMemoryFloatOut_Gui)
	INIT_STATIC_FILE(PatchMemoryBoolOut_Gui)
	INIT_STATIC_FILE(PatchMemoryTextOut_Gui)
	INIT_STATIC_FILE(PatchMemoryList3_Gui);
	INIT_STATIC_FILE(Slider_Gui)
	INIT_STATIC_FILE(Image3_Gui)
	INIT_STATIC_FILE(GUIBoolInverter_Gui)
	INIT_STATIC_FILE(ListEntry4_Gui);
	INIT_STATIC_FILE(PanelGroup_Gui)
	INIT_STATIC_FILE(PatchInfo_Gui)
	INIT_STATIC_FILE(PolyphonyControl_Gui)
	INIT_STATIC_FILE(PopupMenuXP_Gui)
	INIT_STATIC_FILE(TextEntry4_Gui)
	INIT_STATIC_FILE(Increment3_Gui)
	INIT_STATIC_FILE(ListEntry_Gui);
	INIT_STATIC_FILE(PlainImage_Gui)
	INIT_STATIC_FILE(Scope3XP_Gui);
	INIT_STATIC_FILE(FreqAnalyser2_Gui);
//	INIT_STATIC_FILE(FreqAnalyser3_Gui);
	INIT_STATIC_FILE(PatchMemoryBoolOut_Gui);
	INIT_STATIC_FILE(VoltMeter_Gui);
	INIT_STATIC_FILE(FloatToTextGUI_Gui);
	INIT_STATIC_FILE(OversamplingControl_Gui);
	INIT_STATIC_FILE(PatchMemoryBoolGui);
	INIT_STATIC_FILE(PatchMemoryBoolOut_Gui);
#endif

#else
	INIT_STATIC_FILE(MidiToCv2);
	INIT_STATIC_FILE(ug_denormal_detect);
	INIT_STATIC_FILE(ug_denormal_stop);
	INIT_STATIC_FILE(ug_filter_allpass);
	INIT_STATIC_FILE(ug_filter_biquad);
	INIT_STATIC_FILE(ug_filter_sv);
	INIT_STATIC_FILE(ug_logic_decade);
	INIT_STATIC_FILE(ug_logic_shift);
	INIT_STATIC_FILE(ug_math_ceil);
	INIT_STATIC_FILE(ug_math_floor);
	INIT_STATIC_FILE(ug_test_tone);
	INIT_STATIC_FILE(ug_logic_Bin_Count);

#endif
	INIT_STATIC_FILE(MidiToCv2);
	INIT_STATIC_FILE(RegistrationCheck) // has DSP also,but too bad.
	INIT_STATIC_FILE(FloatToVolts2);

#if SE_GRAPHICS_SUPPORT
	INIT_STATIC_FILE(PatchPointsGui)
	INIT_STATIC_FILE(CpuMeterGui);
	INIT_STATIC_FILE(PatchPoints);
#endif

	INIT_STATIC_FILE(VoiceMute);
	INIT_STATIC_FILE(ug_adsr);
	INIT_STATIC_FILE(ug_adder2);
	INIT_STATIC_FILE(ug_clipper);
	INIT_STATIC_FILE(ug_combobox);
	INIT_STATIC_FILE(ug_comparator);
	INIT_STATIC_FILE(ug_container);
	INIT_STATIC_FILE(ug_cross_fade);
	INIT_STATIC_FILE(ug_cv_midi);
	INIT_STATIC_FILE(ug_default_setter);
	INIT_STATIC_FILE(ug_delay);
	INIT_STATIC_FILE(ug_feedback_delays);
	INIT_STATIC_FILE(ug_filter_1pole);
	INIT_STATIC_FILE(ug_filter_1pole_hp);
	INIT_STATIC_FILE(ug_fixed_values);
	INIT_STATIC_FILE(ug_float_to_volts);
	INIT_STATIC_FILE(ug_generic_1_1);
	INIT_STATIC_FILE(ug_io_mod);
	//    INIT_STATIC_FILE(ug_led);
	INIT_STATIC_FILE(ug_logic_counter);
	INIT_STATIC_FILE(StepSequencer2);
	INIT_STATIC_FILE(StepSequencer3); // also in ug_logic_counter.cpp
   
	INIT_STATIC_FILE(ug_logic_gate);
	INIT_STATIC_FILE(ug_logic_not);
	INIT_STATIC_FILE(ug_math_base);
	INIT_STATIC_FILE(ug_midi_automator);
	INIT_STATIC_FILE(ug_midi_controllers);
	INIT_STATIC_FILE(ug_midi_filter);
	//     INIT_STATIC_FILE(ug_midi_monitor);
	INIT_STATIC_FILE(ug_midi_keyboard);
	INIT_STATIC_FILE(ug_midi_to_cv);
	INIT_STATIC_FILE(ug_monostable);
	INIT_STATIC_FILE(ug_multiplier);
	INIT_STATIC_FILE(ug_oscillator2);
	INIT_STATIC_FILE(ug_oscillator_pd);
	INIT_STATIC_FILE(ug_pan);
	INIT_STATIC_FILE(ug_patch_param_setter);
	INIT_STATIC_FILE(ug_patch_param_watcher);
	INIT_STATIC_FILE(ug_peak_det);
	INIT_STATIC_FILE(ug_quantiser);
	INIT_STATIC_FILE(ug_random);
	INIT_STATIC_FILE(ug_sample_hold);
	INIT_STATIC_FILE(ug_slider);
	INIT_STATIC_FILE(ug_switch);
	INIT_STATIC_FILE(ug_system_modules);
	INIT_STATIC_FILE(ug_text_entry);
	INIT_STATIC_FILE(ug_vca);
	INIT_STATIC_FILE(ug_voice_monitor);
	INIT_STATIC_FILE(ug_voice_splitter);
//	INIT_STATIC_FILE(ug_volt_meter);
	INIT_STATIC_FILE(ug_voltage_to_enum);
	INIT_STATIC_FILE(ug_volts_to_float);
	INIT_STATIC_FILE(ug_vst_in);
	INIT_STATIC_FILE(ug_vst_out);
	INIT_STATIC_FILE(ug_wave_player);
	INIT_STATIC_FILE(ug_wave_recorder);

	INIT_STATIC_FILE(DAWSampleRate);
	INIT_STATIC_FILE(MIDI2Converter);
	INIT_STATIC_FILE(MPEToMIDI2);
	INIT_STATIC_FILE(Blob2Test);
    
#if SE_GRAPHICS_SUPPORT
	// temporarily built in SELib to make iterating on this quicker
	INIT_STATIC_FILE(GmpiUiTest);
	INIT_STATIC_FILE(CadmiumModules);
#endif
	// You can include extra plugin-specific modules by placing this define in projucer 'Extra Preprocessor Definitions'
	// e.g. SE_EXTRA_STATIC_FILE_CPP="../PROJECT_NAME/Resources/module_static_link.cpp"
#ifdef SE_EXTRA_STATIC_FILE_CPP
#include SE_EXTRA_STATIC_FILE_CPP
#endif

	initialise_synthedit_extra_modules(passFalse);
}

#include <sstream>
#include "Module_Info3.h"
#include "ug_plugin3.h"
#include "ug_gmpi.h"
#include "BundleInfo.h"
#include "./modules/se_sdk3/MpString.h"
#include "GmpiSdkCommon.h"
#include "SafeMessageBox.h"

#if !defined( _WIN32 )
// mac
#include <dlfcn.h>
#endif

using namespace gmpi;
using namespace gmpi_sdk;
using namespace gmpi_dynamic_linking;

void PluginHolder::load()
{
	if (dllHandle || pluginPath.empty())
		return;

	// Get a handle to the DLL.
#if defined( _WIN32)
	assert(!exists(pluginPath / L"Contents")); // win plugins must be a dll (not it's bundle).

	if (MP_DllLoad(&dllHandle, pluginPath.c_str()))
	{
		DWORD err_code = GetLastError();
		LPTSTR lpMsgBuf = nullptr;
		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			err_code,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
			(LPTSTR)&lpMsgBuf,
			0,
			NULL
		);

		std::wstring errorMessage = lpMsgBuf;
		LocalFree(lpMsgBuf);
	}
#else

	assert(exists(pluginPath / L"Contents")); // mac plugins must be a bundle.

	// Create a path to the bundle
	CFStringRef pluginPathStringRef = CFStringCreateWithCString(NULL, pluginPath.c_str(), kCFStringEncodingUTF8);

	CFURLRef bundleUrl = CFURLCreateWithFileSystemPath(kCFAllocatorDefault,
		pluginPathStringRef, kCFURLPOSIXPathStyle, true);
	if (!bundleUrl) {
		printf("Couldn't make URL reference for plugin\n");
		return;
	}

	// Open the bundle
	dllHandle = (DLL_HANDLE)CFBundleCreate(kCFAllocatorDefault, bundleUrl);
	CFRelease(bundleUrl);
	CFRelease(pluginPathStringRef);

	if (!dllHandle) {
		printf("Couldn't create bundle reference\n");
		return;
	}
#endif
}

gmpi::MP_DllEntry PluginHolder::getFactory()
{
	if (!dllHandle)
		return nullptr;

	const char* gmpi_dll_entrypoint_name = "MP_GetFactory";

	gmpi::MP_DllEntry dll_entry_point{};

#if defined( _WIN32)
	auto r = gmpi_dynamic_linking::MP_DllSymbol(dllHandle, gmpi_dll_entrypoint_name, (void**)&dll_entry_point);
	if (r != gmpi::MP_OK)
		return nullptr;
	return dll_entry_point;
#else
	return (gmpi::MP_DllEntry)CFBundleGetFunctionPointerForName((CFBundleRef)dllHandle, CFSTR("MP_GetFactory"));
#endif
}

void PluginHolder::unload()
{
	if (dllHandle)
	{
#if defined( _WIN32)
		MP_DllUnload(dllHandle);
#else
		CFBundleUnloadExecutable((CFBundleRef)dllHandle);
		CFRelease((CFBundleRef)dllHandle);
#endif
	}
}

PluginHolder::~PluginHolder()
{
	unload();
}

Module_Info3::Module_Info3() // serialisation only
{
}

Module_Info3::Module_Info3( const std::wstring& file_and_dir, const std::wstring& overridingCategory ) :
	filename(file_and_dir)
{
	if( !overridingCategory.empty() )
	{
		m_group_name = overridingCategory;
	}
}

// developer mode only, reload dll if user has updated it
void Module_Info3::ReLoadDll()
{
	holder.unload();
	SetupPlugs();
}

// returns false if dll not available
bool Module_Info3::LoadDllOnDemand()
{
	// if we are loading a project with incompatible modules. Don't attempt to load dll until it's upgraded. Editor-only
	if (m_incompatible_with_current_module)
		return false;

	if (holder.isLoaded())
		return true;

	holder.load();

	if(!holder.isLoaded())
		return false;

	if (!isShellPlugin() || !isSummary()) // shell plugins need info fleshed out.
		return true;

	auto factory = getFactory2();

	if(!factory)
		return false;

	gmpi_sdk::mp_shared_ptr<gmpi::IMpShellFactory> shellFactory;
	auto r = factory->queryInterface(gmpi::MP_IID_SHELLFACTORY, shellFactory.asIMpUnknownPtr());
	gmpi_sdk::MpString s;
	r = shellFactory->getPluginInformation(m_unique_id.c_str(), s.getUnknown());

	// scan XML.
	if (r != gmpi::MP_OK)		// Shell does not have this plugin.
		return false;

	tinyxml2::XMLDocument doc2;
	doc2.Parse(s.c_str());
						
	if( doc2.Error() )
	{
		std::wostringstream oss;
		oss << L"Module XML Error: [SynthEdit.exe]" << doc2.ErrorName() << L"." << doc2.Value();
		SafeMessagebox(0, oss.str().c_str(), L"", MB_OK | MB_ICONSTOP);
		assert(false);
		return false;
	}
	tinyxml2::XMLNode* pluginList = doc2.FirstChildElement("PluginList");

	if (!pluginList) // handle XML without <PluginList> only <Plugin>.
		pluginList = &doc2;

	auto PluginElement = pluginList->FirstChildElement("Plugin");
	if (!PluginElement)
		return false;

	// check for existing
	std::wstring plugin_id = Utf8ToWstring(PluginElement->Attribute("id"));

	// can be caused by saving the same dll with different unique ID. e.g. by saving PD303 from it's prefab twice. Without re-scanning VSTs.
	if (plugin_id != UniqueId())
		return false;

	scanned_xml_dsp = scanned_xml_gui = false; // prevent spurious warnings.
	ScanXml(PluginElement);

	return true;
}

void Module_Info3::LoadDll_old()
{
#if 0
	//	_RPT1(_CRT_WARN, "Module_Info3::LoadDll %s\n", filename );

	// if we are loading a project with incompatible modules. Don't attempt to load dll until it's upgraded. Editor-only
	if (m_incompatible_with_current_module)
		return;

#ifdef _WIN32
    std::wstring load_filename = filename;
#else
    std::wstring load_filename = macSemBundlePath;
#endif
    
//#if !defined( SE_ED IT_SUPPORT ) && defined( SE_TARGET_PLU GIN ) && SE_EXTERNAL_SEM_SUPPORT==1
	{
		// plugin uses relative pathnames, editor uses full paths
		const bool isFullPath = load_filename.find(L":") != std::wstring::npos || (!load_filename.empty() && load_filename[0] == '/');
		if (!isFullPath)
			load_filename = combine_path_and_file(BundleInfo::instance()->getSemFolder(), load_filename);
	}

	// Get a handle to the DLL module.
#if defined( _WIN32)
		if (MP_DllLoad(&dllHandle, load_filename.c_str()))
		{
			// load failed, try it as a bundle.
			const auto bundleFilepath = load_filename + L"/Contents/x86_64-win/" + filename;
			MP_DllLoad(&dllHandle, bundleFilepath.c_str());
		}
#else
    	// int32_t r = MP_DllLoad( &dllHandle, load_filename.c_str() );
	CFStringRef dirs[] = { CFStringCreateWithCString(NULL, fullPath.c_str(), kCFStringEncodingUTF8) };

		// Create a path to the bundle
		CFStringRef pluginPathStringRef = CFStringCreateWithCString(NULL,
			WStringToUtf8( load_filename ).c_str(), kCFStringEncodingUTF8);

		CFURLRef bundleUrl = CFURLCreateWithFileSystemPath(kCFAllocatorDefault,
		pluginPathStringRef, kCFURLPOSIXPathStyle, true);
		if(bundleUrl == NULL) {
			printf("Couldn't make URL reference for plugin\n");
			return;
		}

		// Open the bundle
		dllHandle = (DLL_HANDLE) CFBundleCreate(kCFAllocatorDefault, bundleUrl);
		CFRelease(bundleUrl);
		CFRelease(pluginPathStringRef);

		if(dllHandle == 0) {
			printf("Couldn't create bundle reference\n");
			return;
		}
    	return; // success.
    #endif

	std::wstring errorMessage(L"Error"); // a default error message for mac
    
#if defined( _WIN32)
	// If the handle is valid, we're done
	if(dllHandle != NULL)
	{
		return; // OK
	}

	// some problem
	{
		DWORD err_code = GetLastError();
		LPTSTR lpMsgBuf = nullptr;
		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			err_code,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
			(LPTSTR) &lpMsgBuf,
			0,
			NULL
		);

		errorMessage = lpMsgBuf;
		LocalFree(lpMsgBuf);
	}

#else
    assert(false);
#endif
    
	// avoid bugging user over and over.
	if(!m_module_dll_available)
	{
		return;
	}

	m_module_dll_available = false;

	std::wostringstream oss;
	oss << errorMessage << L": " << load_filename << L".";
	SafeMessagebox(0, oss.str().c_str(), L"", MB_OK|MB_ICONSTOP );
	assert(false);

#endif
}

#if 0
void Module_Info3::Unload()
{
#if defined( _WIN32)
	if (dllHandle != NULL)
	{
		const auto r = MP_DllUnload( dllHandle );

#ifdef _DEBUG
		if (r)
		{
			_RPTW1(_CRT_WARN, L"FAIL!: Module_Info3::Unload %s\n", filename.c_str() );
		}
#endif

		dllHandle = 0;
	}
#else
	if( dllHandle != 0 )
	{
    	CFBundleUnloadExecutable((CFBundleRef) dllHandle);
    	CFRelease((CFBundleRef) dllHandle);
    }
#endif

}
#endif

gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> Module_Info3::getFactory2()
{
	if (!LoadDllOnDemand())
		return {};

	int32_t r{};

	gmpi::MP_DllEntry dll_entry_point = {};
#ifdef _WIN32
	const char* gmpi_dll_entrypoint_name = "MP_GetFactory";
	r = MP_DllSymbol(holder.getHandle(), gmpi_dll_entrypoint_name, (void**)&dll_entry_point);
#else
	dll_entry_point = (gmpi::MP_DllEntry)CFBundleGetFunctionPointerForName((CFBundleRef)dllHandle, CFSTR("MP_GetFactory"));
#endif        

	if (!dll_entry_point)
	{
		return {};
	}

	gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> factory;
	r = dll_entry_point(factory.asIMpUnknownPtr());

	return factory;
}

gmpi::IMpUnknown* Module_Info3::Build( int subType, bool quietFail )
{
	auto com_object = getFactory2();

	if(!com_object)
	{
		return {};
	}

	{
		gmpi_sdk::mp_shared_ptr<gmpi::IMpFactory2> factory;
		const auto r = com_object->queryInterface(gmpi::MP_IID_FACTORY2, factory.asIMpUnknownPtr());
		if (r == gmpi::MP_OK && factory)
		{
			// STEP 4: Ask factory to instantiate plugin.
			gmpi::IMpUnknown* com_object2 = 0;
			int32_t createResult = factory->createInstance2(m_unique_id.c_str(), subType, reinterpret_cast<void**>(&com_object2));

			//				ERROR_CHECK(r,"fail to create plugin\n");
			if (createResult == gmpi::MP_OK)
			{
				return com_object2;
			}
		}
	}

	{
		// GMPI IPluginFactory uses a utf8 ID (not wstring).
		auto gmpi_com_object = (gmpi::api::IUnknown*)com_object.get();
		gmpi::shared_ptr<gmpi::api::IPluginFactory> factory;
		const auto r = gmpi_com_object->queryInterface(&gmpi::api::IPluginFactory::guid, factory.put_void());
		if (r == gmpi::ReturnCode::Ok && factory)
		{
			const auto uniqueIdUtf8 = WStringToUtf8(m_unique_id);
			// STEP 4: Ask factory to instantiate plugin.
			gmpi::api::IUnknown* com_object2{};
			const auto createResult = factory->createInstance(uniqueIdUtf8.c_str(), (gmpi::api::PluginSubtype) subType, reinterpret_cast<void**>(&com_object2));

			// ERROR_CHECK(r,"fail to create plugin\n");
			if (createResult == gmpi::ReturnCode::Ok)
			{
				return (gmpi::IMpUnknown*) com_object2; // these are compatible enough
			}
		}
	}

	if( !quietFail )
	{
		std::wostringstream oss;
		oss << L"Fail to instansiate module class (" << m_unique_id << L"). check RegisterPlugin() matches XML file module id";
		SafeMessagebox(0, oss.str().c_str(), L"", MB_OK | MB_ICONSTOP);
		// ok	assert( false );
	}

	// STEP 7: Unload DLL
	// r = MP_DllUnload( &dll_handle );
	return 0;
}

ug_base* Module_Info3::BuildSynthOb()
{
	if( !LoadDllOnDemand() || !m_dsp_registered )
	{
		return 0;
	}

	auto factoryBase = getFactory2();
    if(!factoryBase)
    {
        // can hapen with Intel-only SEMs on M1
        printf("Couldn't get a pointer to plugin's factory function\n");
        return nullptr;
    }

	int32_t r{};
	// Obtain factory V2 if avail.
	{
		// two step init. create then plugin then host.

		gmpi_sdk::mp_shared_ptr<gmpi::IMpFactory2> factory2;
		r = factoryBase->queryInterface(gmpi::MP_IID_FACTORY2, factory2.asIMpUnknownPtr());

		if (factory2)
		{
			// SE SDK3
			// Ask factory to instantiate plugin
			gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> plugin;
			r = factory2->createInstance2(m_unique_id.c_str(), gmpi::MP_SUB_TYPE_AUDIO, plugin.asIMpUnknownPtr());
			if (!plugin || r != gmpi::MP_OK)
			{
				return {};
			}

			{
				gmpi_sdk::mp_shared_ptr<gmpi::IMpPlugin2> my_plugin2;
				r = plugin->queryInterface(gmpi::MP_IID_PLUGIN2, my_plugin2.asIMpUnknownPtr());

				if (my_plugin2)
				{
					// Now plugin safely created. Host it.
					auto ug = new ug_plugin3<gmpi::IMpPlugin2, gmpi::MpEvent>();
					ug->AttachGmpiPlugin(my_plugin2);
					my_plugin2->setHost(static_cast<gmpi::IGmpiHost*>(ug));

					ug->setModuleType(this);
					ug->flags = static_cast<UgFlags>(ug_flags);
					ug->latencySamples = latency;

					return ug;
				}
			}
		}
		else
		{
			// GMPI SDK
			// uses utf8 ID instead of utf16
			gmpi_sdk::mp_shared_ptr<gmpi::api::IPluginFactory> factory;
			r = factoryBase->queryInterface((const gmpi::MpGuid&) gmpi::api::IPluginFactory::guid, factory.asIMpUnknownPtr());

			if (!factory || r != gmpi::MP_OK)
			{
				return {};
			}

			const auto uniqueIdUtf8 = WStringToUtf8(m_unique_id);

			gmpi::shared_ptr<gmpi::api::IUnknown> plugin;
			const auto r2 = factory->createInstance(uniqueIdUtf8.c_str(), gmpi::api::PluginSubtype::Audio, plugin.put_void());
			if (!plugin || r2 != gmpi::ReturnCode::Ok)
			{
				return {};
			}

			auto gmpi_plugin = plugin.as<gmpi::api::IProcessor>();
			if (!gmpi_plugin)
			{
				return {};
			}

			// Now plugin safely created. Host it.
			auto ug = new ug_gmpi(this, gmpi_plugin);
			ug->flags = static_cast<UgFlags>(ug_flags);
			ug->latencySamples = latency;

			return ug;
		}

		{
			// SynthEdit SEM (modern)

			// Obtain factory.
			gmpi_sdk::mp_shared_ptr<gmpi::IMpFactory> factory;
			{
				r = factoryBase->queryInterface(gmpi::MP_IID_FACTORY, factory.asIMpUnknownPtr());
			}

			auto ug = new ug_plugin3<gmpi::IMpPlugin, gmpi::MpEvent>();
			gmpi::IMpHost* host = static_cast<gmpi::IMpHost*>( ug );

			// STEP 4: Ask factory to instantiate plugin
			gmpi_sdk::mp_shared_ptr<gmpi::IMpUnknown> com_object2; // plugin.
			r = factory->createInstance(( m_unique_id ).c_str(), gmpi::MP_SUB_TYPE_AUDIO, ( gmpi::IMpUnknown* ) host, com_object2.asIMpUnknownPtr());

			//				ERROR_CHECK(r,"fail to create plugin\n");
			if( r == gmpi::MP_OK )
			{
				// STEP 5: Ask plugin to provide SDK3 Processor interface
				gmpi_sdk::mp_shared_ptr<gmpi::IMpPlugin> my_plugin;
				r = com_object2->queryInterface(gmpi::MP_IID_PLUGIN, my_plugin.asIMpUnknownPtr());
				//				ERROR_CHECK(r,"plugin does not support GMPI V 1.0 Interface\n");

				ug->AttachGmpiPlugin(my_plugin);

				ug->setModuleType(this);
				ug->latencySamples = latency;
				ug->flags = static_cast<UgFlags>( ug_flags );

				return ug;
			}
			else
			{
				// !! NOTE this will crash SE as downstream modules won't have anything to connect to.
				// current code uses HasActiveConnections() to avoid this, might be better to check each plug on each ug, deal to any with no connection (after all other connections made)

				std::wostringstream oss;
				oss << L"Fail to instansiate module audio class (" << m_unique_id << L"). check RegisterPlugin() matches XML file module id";
				SafeMessagebox(0, oss.str().c_str(), L"", MB_OK | MB_ICONSTOP);
				assert(false);

				delete ug;
				ug = 0;
			}
		}
	}

	// STEP 7: Unload DLL
	//	r = MP_DllUnload( &dll_handle );

	return 0;
}

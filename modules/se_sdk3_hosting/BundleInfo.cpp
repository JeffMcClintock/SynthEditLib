#include <algorithm>
#include <fstream>
#include <filesystem>
#include "tinyXml2/tinyxml2.h"
#include "BundleInfo.h"
#include "xp_dynamic_linking.h"
#include "unicode_conversion.h"
#include "xplatform.h"
#include "it_enum_list.h"
#include "xp_dynamic_linking.h"
#include "GmpiSdkCommon.h"
#include "Core/GmpiApiAudio.h"

#if defined( _WIN32 )
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "Shlobj.h"
#endif

using namespace JmUnicodeConversions;

#if (GMPI_IS_PLATFORM_JUCE==1)
extern const char* se2juce_getNamedResource(const char* name, int& returnBytes);
// extern const char* se2juce_getIndexedResource(int index, int& returnBytes);
#endif

#if __APPLE__
#include <dlfcn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <pwd.h>
#include <sysdir.h>  // for sysdir_start_search_path_enumeration
#include <glob.h>    // for glob needed to expand ~ to user dir

std::string expandTilde(const char* str) {
    if (!str) return {};

    glob_t globbuf;
    if (glob(str, GLOB_TILDE, nullptr, &globbuf) == 0) {
        std::string result(globbuf.gl_pathv[0]);
        globfree(&globbuf);
        return result;
    } else {
        return {};
    }
}

// ~/Library/Application Support/
std::string settingsPath() {
    char path[PATH_MAX];
    auto state = sysdir_start_search_path_enumeration(SYSDIR_DIRECTORY_APPLICATION_SUPPORT,
                                                      SYSDIR_DOMAIN_MASK_USER);
    if ((state = sysdir_get_next_search_path_enumeration(state, path))) {
        return expandTilde(path);
    } else {
        return {};
    }
}

// inspired by: public.sdk/source/vst/vstguieditor.cpp
//void* gBundleRef = 0;
//static int openCount = 0;
//CFBundleRef getBundleRef(){return (CFBundleRef) gBundleRef;};
//------------------------------------------------------------------------
CFBundleRef CreatePluginBundleRef()
{
    CFBundleRef rBundleRef = 0;
    
	Dl_info info;
	if (dladdr ((const void*)CreatePluginBundleRef, &info))
	{
		if (info.dli_fname)
		{
            std::string name;
			name.assign (info.dli_fname);
			for (int i = 0; i < 3; i++)
			{
				auto p = name.find_last_of ('/');
                if (p == std::string::npos)
				{
					fprintf (stdout, "Could not determine bundle location.\n");
					return 0; // unexpected
				}
//				name.remove (delPos, name.length () - delPos);
                name = name.substr(0, p);

            }
			CFURLRef bundleUrl = CFURLCreateFromFileSystemRepresentation (0, (const UInt8*)name.c_str(), name.length (), true);
			if (bundleUrl)
			{
				rBundleRef = CFBundleCreate (0, bundleUrl);
				CFRelease (bundleUrl);
			}
		}
	}
    
    return rBundleRef;
}

void ReleasePluginBundleRef (CFBundleRef bundleRef)
{
	CFRelease (bundleRef);
}


/*
//------------------------------------------------------------------------
void ReleaseVSTGUIBundleRef ()
{
	openCount--;
	if (gBundleRef)
		CFRelease (gBundleRef);
	if (openCount == 0)
		gBundleRef = 0;
}
 */
/*
CFBundleRef BundleInfo::GetBundle()
{
    return CreatePluginBundleRef();
}
*/
#endif

BundleInfo* BundleInfo::instance()
{
    static BundleInfo singleton;
    return &singleton;
}

BundleInfo::BundleInfo()
{
    initPluginInfo();
}

std::wstring BundleInfo::getSemFolder()
{
#if defined( _WIN32 )

    if (pluginIsBundle)
    {
        const auto path = gmpi_dynamic_linking::MP_GetDllFilename();
        return path.substr(0, path.rfind(L"Contents")) + L"Contents/Plugins/";
    }

    if(semFolder.empty())
        return getImbeddedFileFolder();

    return semFolder; // ref CSynthEditApp::InitInstance()
#else
    std::string result;

    CFBundleRef br = CreatePluginBundleRef();
    
	if ( br ) // getBundleRef ())
	{
        CFURLRef url2 = CFBundleCopyBuiltInPlugInsURL (br);
        char filePath2[PATH_MAX] = "";
        if (url2)
        {
            if (CFURLGetFileSystemRepresentation (url2, true, (UInt8*)filePath2, PATH_MAX))
            {
                result = filePath2;
            }
            CFRelease(url2);
        }
        ReleasePluginBundleRef(br);
    }
    
    return Utf8ToWstring(result.c_str());
#endif
}

std::wstring BundleInfo::getResourceFolder()
{
#if (GMPI_IS_PLATFORM_JUCE==1)
    return {}; // probly should be: Utf8ToWstring(resourceTypeScheme); then we could kill all the special-case stuff mayby
    // TODO should be a subclass or something, shouldn't even be looking for a folder
#endif

#if defined( _WIN32 )
    if (pluginIsBundle)
    {
        // loading resource from bundle resource folder.
        const auto path = gmpi_dynamic_linking::MP_GetDllFilename();
        return path.substr(0, path.find(L"Contents")) + L"Contents/Resources/";
    }

    return getImbeddedFileFolder();
#else
    std::string result;
    
    CFBundleRef br = CreatePluginBundleRef();
    
    if ( br )
    {
        CFURLRef url2 = CFBundleCopyResourcesDirectoryURL (br);
        char filePath2[PATH_MAX] = "";
        if (url2)
        {
            if (CFURLGetFileSystemRepresentation (url2, true, (UInt8*)filePath2, PATH_MAX))
            {
                result = filePath2;
            }
            CFRelease(url2);
        }
        ReleasePluginBundleRef(br);
    }
    
    return Utf8ToWstring(result.c_str());
#endif
}

std::wstring BundleInfo::getImbeddedFileFolder()
{
	std::wstring result;

#if defined( _WIN32 )

	/*
	// VST VERSION STORE IMBEDDED FILES IN A SUB-FOLDER WITH SAME NAME AS DLL
	std::wstring sub_folder = StripExtension(StripPath(std::wstring(full_path)));
	default_path = default_path + sub_folder + (L"\\");
	*/

	result = gmpi_dynamic_linking::MP_GetDllFilename();
	// Chop off trailing filename
	size_t p = result.find_last_of('\\') + 1;  // VST plugin is already in sub-folder. Strip off filename, keep slash.
	result = result.substr(0, p);

#else // Mac.
    
    result = getSemFolder();

	size_t p = result.find_last_of('/') + 1;  // Strip off ", keep slash.
	result = result.substr(0, p) + L"Resources/";

#endif

	return result;
}

// Plugins only.
std::wstring BundleInfo::getUserDocumentFolder()
{
#if defined( _WIN32 )

	// Correct folder (user presets). [MYDOCUMENTS]/VST3 Presets/$COMPANY/$PLUGIN-NAME/
	wchar_t myDocumentsPath[MAX_PATH];
	SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, myDocumentsPath);
	std::wstring myDocuments(myDocumentsPath);
	std::wstring folderPath{ myDocuments };
	folderPath += L"\\";

	return folderPath;

#else // Mac.
    const char *homeDir = getenv("HOME");
    
    if(homeDir)
        return Utf8ToWstring(homeDir);
    
    const struct passwd* pwd = getpwuid(getuid());
    if (pwd)
        return Utf8ToWstring(pwd->pw_dir);
    
	return {};
#endif
}

std::filesystem::path BundleInfo::getSettingsFolder()
{
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    SHGetFolderPath(NULL, CSIDL_COMMON_APPDATA, NULL, SHGFP_TYPE_CURRENT, path);
    return path;
#else
    return settingsPath();
#endif
}

std::filesystem::path BundleInfo::getPlatformPluginsFolder()
{
#ifdef _WIN32
    wchar_t path[MAX_PATH];
    SHGetFolderPath(NULL, CSIDL_PROGRAM_FILES_COMMON, NULL, SHGFP_TYPE_CURRENT, path);
    return path;
#else
    return "/Library/Audio/Plugins/";
#endif
}

void BundleInfo::initPresetFolder(const char* manufacturer, const char* product)
{
#ifdef _WIN32
    std::wstring res{ L"C:\\ProgramData\\" };
#else
//#if defined( GMPI_IS_PLATFORM_JUCE )
	// "~/Library/Application Support/" solves issues with permissions in macOS
    auto res = Utf8ToWstring(settingsPath()) + PLATFORM_PATH_SLASH_L;
//#else
//    std::wstring res{ L"/Library/Application Support/" };
//#endif

#endif

    res += Utf8ToWstring(manufacturer) + PLATFORM_PATH_SLASH_L + Utf8ToWstring(product) + PLATFORM_PATH_SLASH_L;
    res += std::wstring(L"USER Presets") + PLATFORM_PATH_SLASH_L;

    presetFolder = res;
}

#if 0
std::wstring BundleInfo::getPresetFolder()
{
    // TODO: Mac VST?
#if defined( _WIN32 )
    // Correct folder (user presets). [MYDOCUMENTS]/VST3 Presets/$COMPANY/$PLUGIN-NAME/
    wchar_t myDocumentsPath[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, SHGFP_TYPE_CURRENT, myDocumentsPath);
    std::wstring myDocuments(myDocumentsPath);
    std::wstring vst3PresetFolder{ myDocuments };

    vst3PresetFolder += L"VST3 Presets\\";

    // Add Company name.
    auto factory = MyVstPluginFactory::GetInstance();

    vst3PresetFolder =
        vst3PresetFolder +
        Utf8ToWstring(factory->getVendorName()) + L"\\" +       // Vendor
        Utf8ToWstring(factory->getProductName()) + L"\\";       // Product.

    return vst3PresetFolder;
#else
    // Mac.
    assert(!info_.vendorName.empty());
    
	std::wstring result;

    // User's ~/Library/Audio/Presets/Vendor/MySynth/
    std::wstring pluginName;
    {
        auto dllname = gmpi_dynamic_linking::MP_GetDllFilename();
        
        auto p1 = dllname.find(L".component/");
        pluginName = dllname.substr(0, p1);
        p1 = pluginName.find_last_of('/') + 1;
        pluginName = pluginName.substr(p1);
    }
    
    result = getUserDocumentFolder();
    
    // Note: !!! This folder may not exist, which will result in presets being saved in 'Documents' (and never scanned by preset browser)
    // might be prefereable to check if this directory exists, and if not fallback to 'Documents' on mac
    result = result + L"/Library/Audio/Presets/";

	result = result + Utf8ToWstring(info_.vendorName) + L"/"+ pluginName + L"/";

	return result;
#endif
}
#endif

std::string sanitizedResourceIdString(const char* resourceId)
{
    std::string s(resourceId);
    std::replace(s.begin(), s.end(), '.', '_');
    std::replace(s.begin(), s.end(), ' ', '_');

    return s;
}

// determin if an EMBEDDED resource exists
bool BundleInfo::ResourceExists([[maybe_unused]] const char* resourceId)
{
#if (GMPI_IS_PLATFORM_JUCE==1)
    int unused = 0;
    return se2juce_getNamedResource(sanitizedResourceIdString(resourceId).c_str(), unused);
#endif

    return false;
}

std::string BundleInfo::getResource( const char* resourceId )
{
#if (GMPI_IS_PLATFORM_JUCE==1)
	int size = 0;
	auto data = se2juce_getNamedResource(sanitizedResourceIdString(resourceId).c_str(), size);
	return std::string(data, static_cast<size_t>(size));
#else

#if defined(_WIN32)

	HMODULE ghInst;
	{
		gmpi_dynamic_linking::DLL_HANDLE temp = 0;
		gmpi_dynamic_linking::MP_GetDllHandle(&temp);
		ghInst = (HMODULE)temp;
	}

	// Load SynthEdit project file from resource.
	std::wstring resourceIdw = JmUnicodeConversions::Utf8ToWstring(resourceId);
	HRSRC hRsrc = ::FindResourceW(ghInst, resourceIdw.c_str(), L"DATA" );
    if (hRsrc)
    {
        auto size = SizeofResource(ghInst, hRsrc);
        BYTE* lpRsrc = (BYTE*)LoadResource(ghInst, hRsrc);
        BYTE* locked_mem = (BYTE*)LockResource((HANDLE)lpRsrc);

        const char* projectFile = (char*)locked_mem;
        std::string r(projectFile, (size_t)size);

        UnlockResource((HANDLE)lpRsrc);
        FreeResource((HANDLE)lpRsrc);

        return r;
    }

    // fallback to loading resource from bundle resource folder.
	const auto path = getResourceFolder() + Utf8ToWstring(resourceId);

    {
		// Open the stream to 'lock' the file.
		std::ifstream f(path, std::ios::in | std::ios::binary);

        if (f.fail())
        {
            _RPTN(_CRT_WARN, "BundleInfo::getResource() failed to open file '%s' : %s\n", resourceId, strerror(errno));
            return {};
        }

		// Obtain the size of the file.
		f.seekg(0, std::ios::end);
		const size_t fileSize = f.tellg();
		f.seekg(0);

		// Create a buffer.
		std::string result(fileSize, '\0');

		// Read the whole file into the buffer.
		f.read(result.data(), fileSize);

        return result;
    }

#else
    // NOTE: On IOS, resources are in the main app folder, not in the Plugin (appx) which is in the Plugins folder of the main app. e.g. SE_IOS_APP.app/Plugins/SE_IOS_audiounit.appx/...
    // So in XCode add resources to the APP not the appx target.
    std::string result;
    
	if ( resourceId == 0 )
		return "";
    
	if ( resourceId[0] == '/')
	{
		// it's an absolute path, we can use it as is
		// platformHandle = fopen (res.u.name, "rb");
	}
    CFBundleRef br = CreatePluginBundleRef();
	if (br ) // getBundleRef ())
	{
		CFStringRef cfStr = CFStringCreateWithCString (NULL, resourceId, kCFStringEncodingUTF8);
		if (cfStr)
		{
			CFURLRef url = CFBundleCopyResourceURL (br, cfStr, 0, NULL);
			if (url)
			{
				char filePath[PATH_MAX];
				if (CFURLGetFileSystemRepresentation (url, true, (UInt8*)filePath, PATH_MAX))
				{
					FILE* fp = fopen (filePath, "rb");
                    //char *source = NULL;
                    long bufsize = 0;
                    if (fp != NULL) {
                        /* Go to the end of the file. */
                        if (fseek(fp, 0L, SEEK_END) == 0) {
                            /* Get the size of the file. */
                            bufsize = ftell(fp);
                            if (bufsize == -1) { /* Error */ }
                            
                            /* Allocate our buffer to that size. */
                            result.resize(bufsize);
                            
                            /* Go back to the start of the file. */
                            if (fseek(fp, 0L, SEEK_SET) == 0) { /* Error */ }
                            
                            /* Read the entire file into memory. */
                            size_t newLen = fread((void*)result.data(), sizeof(char), bufsize, fp);
                            if (newLen == 0) {
                                fputs("Error reading file", stderr);
                            }
                        }
                        fclose(fp);
                    }
  				}
				CFRelease (url);
			}
			CFRelease (cfStr);
		}
        ReleasePluginBundleRef(br);
	}
	return result;
#endif
#endif // JUCE
}

int32_t BundleInfo::getPluginId() // 4-char VST2 code to identify presets.
{
    assert(-1 != info_.pluginId || isEditor);
	return info_.pluginId;
}

const BundleInfo::pluginInformation& BundleInfo::getPluginInfo()
{
    return info_;
}

void BundleInfo::initPluginInfo()
{
	const std::filesystem::path path(gmpi_dynamic_linking::MP_GetDllFilename());

    // are we in the editor?
	auto filename = path.filename().wstring();
    {
        isEditor =
            filename.find(L"SynthEdit2.exe") != std::string::npos ||
            filename.find(L"SynthEdit.") != std::string::npos ||
            filename.find(L"TIDE") == 0;
    }

//	pluginIsBundle = path. .find(L".vst3/Contents") != std::string::npos || path.find(L".vst3\\Contents") != std::string::npos;

    // are we in a bundle? path contains "*.vst3/Contents/"
	std::string bundleExtension;
    for(auto it = path.begin(); it != path.end(); ++it)
    {
        if( *it == "Contents" && bundleExtension == "vst3" )
        {
            pluginIsBundle = true;
            break;
		}
        
        bundleExtension = (*it).extension().string();
	}

    if (isEditor)
        return;

    // Plugins load factory xml
    const auto factoryXml = getResource("factory.se.xml");

    tinyxml2::XMLDocument doc;
    doc.Parse(factoryXml.c_str());

    if (doc.Error())
    {
        assert(false);
        return;// initPluginInfoFromWrappedSem();
    }

    {
        // block: Vendor
        auto pElem = doc.FirstChildElement();

        // should always have a valid root but handle gracefully if it does
        if (pElem)
        {
            assert(strcmp(pElem->Value(), "Factory") == 0);

            const auto vendorE = pElem->FirstChildElement( "Vendor" );
            info_.vendorName = vendorE->Attribute("Name");

            auto plugins = pElem->FirstChildElement("Plugins");
            assert(plugins); // Got to have one.
            auto pluginNode = plugins->FirstChildElement("Plugin");
            assert(pluginNode); // Got to have one.

            auto plugin = pluginNode->ToElement();

            // plugin->QueryStringAttribute("Name", &info_.pluginName);
            const char* temp{};
            if (tinyxml2::XML_SUCCESS == plugin->QueryStringAttribute("Name", &temp))
            {
                info_.pluginName = temp;
            }

            // plugin->QueryStringAttribute("subCategories", &info_.subCategories);
            if (tinyxml2::XML_SUCCESS == plugin->QueryStringAttribute("subCategories", &temp))
            {
                info_.subCategories = temp;
            }
            // plugin->QueryStringAttribute("ManufacturerId", &info_.manufacturerId);
            if (tinyxml2::XML_SUCCESS == plugin->QueryStringAttribute("ManufacturerId", &temp))
            {
                info_.manufacturerId = temp;
            }
            std::string tempId;
            //plugin->QueryStringAttribute("PluginID", &tempId);
            if (tinyxml2::XML_SUCCESS == plugin->QueryStringAttribute("PluginID", &temp))
            {
                info_.pluginId = static_cast<int32_t>(std::stoul(temp, nullptr, 10));
            }

            plugin->QueryBoolAttribute("outputsAsStereoPairs", &info_.outputsAsStereoPairs);
            plugin->QueryBoolAttribute("monoUseOk", &info_.monoUseOk);
            plugin->QueryBoolAttribute("emulateIgnorePC", &info_.emulateIgnorePC);
            plugin->QueryIntAttribute("numMidiIn", &info_.midiInputCount);
            if(info_.midiInputCount)
                plugin->QueryBoolAttribute("vst316ChanCc", &info_.vst3Emulate16ChanCcs);

            {
                int latencyConstraint{};
                plugin->QueryIntAttribute("latencyCompensation", &latencyConstraint);
                switch (latencyConstraint)
                {
                case 2:
                    info_.latencyConstraint = ElatencyContraintType::Full;
                    break;
                case 1:
                    info_.latencyConstraint = ElatencyContraintType::Constrained;
                    break;
               default:
                    info_.latencyConstraint = ElatencyContraintType::Off;
                    break;
                };
            }

            plugin->QueryIntAttribute("inputCount", &info_.inputCount);
            plugin->QueryIntAttribute("outputCount", &info_.outputCount);

            {
                const auto outputNameList = plugin->Attribute("outputNames");
                it_enum_list it(JmUnicodeConversions::Utf8ToWstring(outputNameList));
                for (it.First(); !it.IsDone(); it.Next())
                {
                    auto e = it.CurrentItem();
                    info_.outputNames.push_back(JmUnicodeConversions::WStringToUtf8(e->text));
                }
            }
        }
    }
    
    //preset folder for AU2 plugins
    {
        // User's ~/Library/Audio/Presets/Vendor/MySynth/
        auto dllname = gmpi_dynamic_linking::MP_GetDllFilename();

        auto p1 = dllname.find(L".component/");
        if(p1 != std::string::npos)
        {
            auto pluginName = dllname.substr(0, p1);
            p1 = pluginName.find_last_of(L"\\/") + 1;
            pluginName = pluginName.substr(p1);

            auto result = getUserDocumentFolder();
            
            // Note: !!! This folder may not exist, which will result in presets being saved in 'Documents' (and never scanned by preset browser)
            // might be preferable to check if this directory exists, and if not fallback to 'Documents' on mac
            result = result + L"/Library/Audio/Presets/";

            presetFolder = result + Utf8ToWstring(info_.vendorName) + L"/"+ pluginName + L"/";
         }
    }
}

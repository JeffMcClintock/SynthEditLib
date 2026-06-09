/*
#include "BundleInfo.h"

BundleInfo::instance()->getResource("whatever");
*/

#ifndef BundleInstance_h
#define BundleInstance_h

#include <string>
#include <vector>
#include <filesystem>
#include "ElatencyContraintType.h"

#if !defined( _WIN32 )
#include <CoreFoundation/CFBundle.h>
CFBundleRef CreatePluginBundleRef();
#endif

std::string getSettingsFolder();
std::string getPlatformPluginsFolder();

class BundleInfo
{
public:
    struct pluginInformation
    {
        int32_t pluginId = -1;
        std::string manufacturerId;
        std::string processorId; // VST3 component UUID (registry-string form). Needed to write .vstpreset files.
        std::string macCategory; // Audio Unit type code e.g. "aumu"/"aufx". Needed to write .aupreset files.
        int32_t inputCount;
        int32_t outputCount;
        int32_t midiInputCount;
        ElatencyContraintType latencyConstraint;
        std::string pluginName;
        std::string vendorName;
        std::string subCategories;
        bool outputsAsStereoPairs;
        bool emulateIgnorePC = {};
        bool monoUseOk;
        bool vst3Emulate16ChanCcs;
        std::vector<std::string> outputNames;
    };
 
private:
    pluginInformation info_ = {};
    
    void initPluginInfo();
    BundleInfo();
    
public:
    inline static const char* resourceTypeScheme = "res://";
    std::wstring semFolder;
    std::wstring presetFolder; // native format

    bool pluginIsBundle = true;
    bool isEditor = false;
    // True when the factory folder was set by USER intent (test harness;
    // SynthEditCL's `-factorysemsfolder` arg) rather than the app's own
    // natural default. SemCacheName() reads this to pick a per-override
    // Plugin-Cache filename so the user's installed-app cache isn't clobbered
    // by a dev/test run, and so repeat runs against the same override folder
    // share one cache (e.g. a 20-test sweep scans once, not 20 times).
    bool isSemFolderOverridden = false;
    
    static BundleInfo* instance();
    
#if !defined( _WIN32 )
//	CFBundleRef GetBundle();
    void setVendorName(const char* name)
    {
        info_.vendorName = name;
    }
#endif
    void setPluginId(int32_t id)
    {
        info_.pluginId = id;
    }

    bool ResourceExists(const char* resourceId);
	std::string getResource(const char* resourceId);

    // special folders
    void initPresetFolder(const char* manufacturer, const char* product);
    std::wstring getPresetFolder()
    {
        return presetFolder;
    }
    // Machine-wide ("all users") preset folder — the counterpart to the per-user
    // getPresetFolder(). macOS: /Library/Audio/Presets/<vendor>/<plugin>/.
    // Windows: <PublicDocuments>/VST3 Presets/<vendor>/<plugin>/. Derived from
    // getPresetFolder() by swapping the per-user root for the shared root.
    // Returns empty if it can't be derived.
    std::wstring getGlobalPresetFolder();
	std::filesystem::path getUserDocumentFolder();
	// Shared, all-users document folder for resources installed once per machine
	// (skins, prefabs). Windows: C:\Users\Public\Documents (CSIDL_COMMON_DOCUMENTS).
	std::filesystem::path getCommonDocumentFolder();
    std::filesystem::path getBundleContentsFolder();
	std::wstring getImbeddedFileFolder();
    std::wstring getResourceFolder();
	std::wstring getSemFolder();
    
	int32_t getPluginId(); // 4-char VST2 code to identify presets.
    const pluginInformation& getPluginInfo();
	std::filesystem::path getPluginPath(); // root folder/file of plugin. e.g. .../Common Files/VST3/MyPlugin.vst3
};

#endif /* BundleInstance_h */

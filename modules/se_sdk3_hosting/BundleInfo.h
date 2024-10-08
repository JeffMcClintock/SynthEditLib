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

class BundleInfo
{
public:
    struct pluginInformation
    {
        int32_t pluginId = -1;
        std::string manufacturerId;
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
    
//    void initPluginInfoFromWrappedSem();
    void initPluginInfo();
    BundleInfo();
    
public:
    inline static const char* resourceTypeScheme = "res://";
    std::wstring semFolder;
    std::wstring presetFolder; // native format

    bool pluginIsBundle = true;
    bool isEditor = false;
    
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
	std::wstring getUserDocumentFolder();
	std::wstring getImbeddedFileFolder();
    std::wstring getResourceFolder();
	std::wstring getSemFolder();
    std::filesystem::path getSettingsFolder();
    std::filesystem::path getPlatformPluginsFolder();

	int32_t getPluginId(); // 4-char VST2 code to identify presets.
    const pluginInformation& getPluginInfo();
};

#endif /* BundleInstance_h */

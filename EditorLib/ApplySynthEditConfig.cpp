#include "ApplySynthEditConfig.h"
#include "SynthEditAppBase.h"
#include "BundleInfo.h"
#include "ModuleFactory_Editor.h"
#include <filesystem>

namespace
{
// A relative folder on the command line means relative to where the tool was started.
// Resolve it once, here: later code reads a relative module path as a bare filename and
// prefixes the sem folder again ("./modules/./modules/Controls.gmpi"), so every .gmpi
// failed to load and exports silently lost the modules they need.
std::wstring absoluteFolder(const std::string& folder)
{
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(std::filesystem::path(Utf8ToWstring(folder)), ec);
    return ec ? Utf8ToWstring(folder) : absolute.lexically_normal().wstring();
}
}

void ApplyConfigPreInit(CSynthEditAppBase& app, const SynthEditConfig& cfg)
{
    if (cfg.quiet)
        app.SetQuiet();

    // Opt-in VST scanning. LoadOrScanModuleData defaults to skipping VSTs;
    // callers that need them (e.g. CancellationRunner) pass -rescanvsts.
    app.rescanIncludesVsts = cfg.rescanIncludesVsts;

    // -factorysemsfolder routes through SemCacheName(), so it MUST be set
    // before ClearModuleDataCache below — otherwise the cache file deleted
    // is the user's installed-app cache, not the override.
    if (!cfg.overrideFactorySemsFolder.empty())
    {
        BundleInfo::instance()->semFolder             = absoluteFolder(cfg.overrideFactorySemsFolder);
        BundleInfo::instance()->isSemFolderOverridden = true;
    }

    // --rescan: drop the cache so InitInstance regenerates fresh module data.
#ifndef SE_NO_EXTERNAL_MODULES   // BACKLOG S1b -- no cache exists to clear
    if (cfg.rescanModules)
        ClearModuleDataCache();
#endif // SE_NO_EXTERNAL_MODULES

    if (!cfg.overrideSamplesFolder.empty())
        app.overrideSamplesFolder = absoluteFolder(cfg.overrideSamplesFolder);
}

void ApplyConfigPostInit(CSynthEditAppBase& app, const SynthEditConfig& cfg, bool rescanAlreadyHandled)
{
    // In-place rescan path (Windows): cache-clear in PreInit wasn't possible
    // because args were parsed after InitInstance already loaded modules.
    if (!rescanAlreadyHandled && cfg.rescanModules)
        app.RefreshModuleData(true, true, true);

    if (!cfg.rego.empty() && !cfg.regoname.empty())
        app.setTemporaryRegistration(Utf8ToWstring(cfg.regoname), Utf8ToWstring(cfg.rego));
}

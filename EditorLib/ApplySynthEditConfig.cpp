#include "ApplySynthEditConfig.h"
#include "SynthEditAppBase.h"
#include "BundleInfo.h"
#include "ModuleFactory_Editor.h"

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
        BundleInfo::instance()->semFolder             = Utf8ToWstring(cfg.overrideFactorySemsFolder);
        BundleInfo::instance()->isSemFolderOverridden = true;
    }

    // --rescan: drop the cache so InitInstance regenerates fresh module data.
    if (cfg.rescanModules)
        ClearModuleDataCache();

    if (!cfg.overrideSamplesFolder.empty())
        app.overrideSamplesFolder = Utf8ToWstring(cfg.overrideSamplesFolder);
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

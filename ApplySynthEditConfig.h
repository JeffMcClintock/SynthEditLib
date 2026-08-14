#pragma once
#include "ParseSynthEditArgs.h"

class SynthEditApp;

// Two halves of "apply SynthEditConfig to SynthEditApp", split by lifecycle.
//
// Why split? Mac and SynthEditCL parse args before InitInstance(); Windows
// parses them after (args arrive via XAML's deferred OnLoaded). The pre-init
// path implements --rescan as ClearModuleDataCache (InitInstance then sees no
// cache and re-scans). The post-init path implements --rescan as
// RefreshModuleData (in-place) because the cache-clear would be too late to
// affect the already-loaded module set.
//
// CL/Mac usage:    ApplyConfigPreInit(app, cfg);  app.InitInstance();  ApplyConfigPostInit(app, cfg, /*rescanAlreadyHandled=*/true);
// Windows usage:                                  app.InitInstance();  ApplyConfigPostInit(app, cfg, /*rescanAlreadyHandled=*/false);

// Apply config that must take effect BEFORE SynthEditApp::InitInstance().
// Sets quiet; applies --factorysemsfolder via BundleInfo; clears the module
// cache for --rescan (so InitInstance regenerates); pre-seeds the samples-
// folder override on the app instance.
void ApplyConfigPreInit(SynthEditApp& app, const SynthEditConfig& cfg);

// Apply config that needs InitInstance() to have completed. Always-safe set:
// rego/regoname → setTemporaryRegistration. If `rescanAlreadyHandled` is
// false, also performs an in-place RefreshModuleData when --rescan was set.
void ApplyConfigPostInit(SynthEditApp& app, const SynthEditConfig& cfg, bool rescanAlreadyHandled);

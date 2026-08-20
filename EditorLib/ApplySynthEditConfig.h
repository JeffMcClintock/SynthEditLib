#pragma once
#include "ParseSynthEditArgs.h"

class CSynthEditAppBase;

// Two halves of "apply SynthEditConfig to the app", split by lifecycle.
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
//
// BACKLOG C14. These took `SynthEditApp&` until 2026-08-20, which was both a
// carve-out blocker and a latent bug:
//
//   * SynthEditApp is declared in the PRIVATE repo (SE16/SynthEdit2), so this
//     public translation unit was the last one that could not compile from a
//     clean public clone -- C7's whole acceptance test.
//   * There are TWO unrelated classes named `SynthEditApp` in the global
//     namespace -- SE16/SynthEdit2/SynthEditApp.h and
//     SE16/SynthEditCL/SynthEditAppCl.h -- and BOTH were passed to these
//     functions, which are compiled exactly once, into EditorLib, against the
//     former. That is an ODR violation that linked cleanly only because
//     CSynthEditAppBase is the first base of both, so the subobject the body
//     actually touched sat at offset 0 in each.
//
// Every member these functions use is declared in THIS repo -- SetQuiet and
// setTemporaryRegistration on CSynthEditAppBase, rescanIncludesVsts,
// overrideSamplesFolder and RefreshModuleData on its ApplicationBase base --
// so naming the base costs callers nothing (derived-to-base reference
// conversion is implicit) and closes both problems at once. Do not widen this
// back to a concrete app type.

// Apply config that must take effect BEFORE the app's InitInstance().
// Sets quiet; applies --factorysemsfolder via BundleInfo; clears the module
// cache for --rescan (so InitInstance regenerates); pre-seeds the samples-
// folder override on the app instance.
void ApplyConfigPreInit(CSynthEditAppBase& app, const SynthEditConfig& cfg);

// Apply config that needs InitInstance() to have completed. Always-safe set:
// rego/regoname → setTemporaryRegistration. If `rescanAlreadyHandled` is
// false, also performs an in-place RefreshModuleData when --rescan was set.
void ApplyConfigPostInit(CSynthEditAppBase& app, const SynthEditConfig& cfg, bool rescanAlreadyHandled);

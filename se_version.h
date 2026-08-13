#pragma once

// Cache-invalidation stamp for the on-disk caches this library owns:
//
//   ModuleFactory_Editor.cpp -- the plugin/module cache XML ("build_number"
//                               attribute); a mismatch forces a rescan.
//   SkinMgr.cpp              -- the copied skin folder (".resource_version");
//                               a mismatch re-copies the built-in skins.
//
// Deliberately NOT SynthEdit's product version. This repo is public and shared
// -- TIDE Rack's release cycle is not SynthEdit's -- so it cannot include the
// private SE16/se_build_number.h, which stays where SynthEdit's three release
// workflows grep for it. See BACKLOG C9 (resolved option (c), 2026-08-13).
//
// The value is injected by the hosting application, because that is what the
// two caches actually track: SkinMgr copies skins out of the *application's*
// Resources folder, and the module cache lists the modules the *application*
// links. Keying either on a constant this library owned would stop SynthEdit
// invalidating them on upgrade, which is a behaviour regression, not a
// decoupling. SynthEdit's builds pass its se_build_number.h value through
// SE_APP_BUILD_NUMBER (EditorLib/CMakeLists.txt does the injection); a consumer
// that defines nothing gets 0, meaning "this application never invalidates on
// upgrade".
//
// Today every CMake consumer -- TIDE included -- links the one EditorLib that
// carries the injection, so they all see SynthEdit's number, exactly as they
// did before this header existed. The 0 default is what a clean-clone build
// with no access to the private repo will get, i.e. TIDE from C7 onward, which
// wants no skin folder and no module cache anyway.

#ifndef SE_APP_BUILD_NUMBER
#define SE_APP_BUILD_NUMBER 0
#endif

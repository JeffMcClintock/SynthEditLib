#pragma once

// all obsolete?
// replaced by notification e.g. 		Document()->Application()->VO_Notify(OM_SHOW_CODE_SKELETON_DIALOG, (void*) static_cast<intptr_t>(Handle()));

void doDialogConnectUg(class CUG*);
void doDialogPatchManager(class CUG_with_patches*);

// True if this app implements the module-editor dialogs. Only the WinUI3
// desktop app does; TIDE, SynthEditCL and the tests link stubs that do
// nothing, so offering a menu entry that reaches them offers the user a
// command that cannot work (BACKLOG S3g).
//
// A runtime query rather than an #ifdef, because EditorLib compiles once for
// every app -- the same shape, and the same reason, as GetLicenseState()
// (BACKLOG C11). Defined once per app, so a new app that links EditorLib gets
// a link error until it answers rather than silently inheriting a default.
//
// "Connect..." is not gated on this: it is a low-priority SynthEdit 1.5
// feature nothing implements yet, so its menu entry is simply commented
// out. It can move behind this query if SynthEdit ever ships it.
bool AppHasModuleEditorDialogs();

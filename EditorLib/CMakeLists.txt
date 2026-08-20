cmake_minimum_required(VERSION 3.30)

project(EditorLib)

# C6 moved this file into the public SynthEditLib repo, so it may not name a
# path in the private SE16 tree. EDITOR_DIR and EDITOR2_DIR both pointed at
# SE16/SynthEdit2 and, after C12d took the last three entries out, had ZERO
# uses left in this file -- they are deleted rather than relocated.
#
# Every source below lives in the SynthEditLib repo, which is this file's own
# parent directory. SE16 sets syntheditlib_folder to its checkout (which may be
# elsewhere, via SYNTHEDITLIB_FOLDER_OVERRIDE), so prefer that when it is set
# and fall back to the containing repo when this is built standalone.
if(syntheditlib_folder)
    set( SYNTHEDITLIB_DIR ${syntheditlib_folder} )
else()
    set( SYNTHEDITLIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.. )
endif()

# Create static library with shared source files
add_library(EditorLib STATIC
    # SynthEditLib hosting modules
    ${SYNTHEDITLIB_DIR}/modules/se_sdk3_hosting/ConnectorViewStruct.h
    ${SYNTHEDITLIB_DIR}/modules/se_sdk3_hosting/ConnectorViewStruct.cpp  
    ${SYNTHEDITLIB_DIR}/modules/se_sdk3_hosting/ContainerViewStruct.h
    ${SYNTHEDITLIB_DIR}/modules/se_sdk3_hosting/ContainerViewStruct.cpp
    ${SYNTHEDITLIB_DIR}/modules/se_sdk3_hosting/ModuleViewStruct.h
    ${SYNTHEDITLIB_DIR}/modules/se_sdk3_hosting/ModuleViewStruct.cpp
    ${SYNTHEDITLIB_DIR}/modules/se_sdk3_hosting/ProcessorWatchdog.h
    ${SYNTHEDITLIB_DIR}/SafeMessageBox.h
    
    # SynthEdit source files.
    #
    # The list is kept in one alphabetical run, so carve-out progress shows up as
    # the prefix rather than as a separate block: ${EDITOR_DIR} entries still live
    # in the private SynthEdit2 folder, ${SYNTHEDITLIB_DIR} entries have already
    # moved to the public repo. Stage C2 moved the leaf files (checkpoint,
    # cpu_accumulator, FrameRateLogger, imbedded_file, it_*, FuzzyMatch.h); C3
    # the document model, C4 the views and browsers, C5 the app base. C6 moves
    # this file itself.
    #
    # C5 did NOT leave the list fully converted, and the old wording here
    # ("C3-C5 convert the rest") said it would. C12 owns the remainder, and
    # is split into six sub-stages -- C12a-C12f. The plan, the per-stage file
    # lists and the measurements behind the split are in the TideSynth repo,
    # docs/c12-remaining-editor-files.md; keep that doc and this list in step.
    #
    # C12f moved the patch cluster -- PatchManager, PatchParameter,
    # PatchParameter_host_generated, UG2 and CPlugin (.cpp + .h each), ten
    # entries and 6,298 lines, the largest single stage of the carve-out and
    # 64% of C12 by volume -- into SynthEditLib's root. It had to move as one
    # commit: the first four are a strongly-connected component
    # (PatchManager <-> PatchParameter <-> PatchParameter_host_generated, and
    # PatchManager <-> UG2), so no split leaves both halves whole; CPlugin
    # hangs off PatchManager and rode along. Closed the last 14 dangling
    # includes SynthEditLib had into this folder, 21 -> 7 measured.
    #
    # C12c moved the independent leaves -- CLine2, commandMgr, SuspendDSP,
    # legacyExternalApp and ModuleDragAndDropManager (.cpp + .h each) plus the
    # header-only ui_msg_target.h and IGuiHost.h, twelve entries and 1,316
    # lines -- into SynthEditLib's root. Largest dangling reduction of any
    # sub-stage: 21 edges closed, 42 -> 21 measured. InterfaceObject_editor and
    # platform_editor look like leaves and are deliberately NOT here; they are
    # C12d, which is marked linux because their link topology is invisible to
    # MSVC.
    #
    # C12b moved the five controls -- Control, Ctl_Combo, Ctl_Keyboard2,
    # Ctl_Slider, Ctl_Text, ten entries and 1,053 lines -- into SynthEditLib's
    # root, closing the six dangling includes ModuleFactory_Editor.cpp (4) and
    # CContainer.cpp (2) had into the private repo.
    #
    # C12a (done) delisted four entries rather than moving them, because they
    # are dead or duplicated, not private-but-needed: GuiPin.h has no includer
    # anywhere and could not compile if it did; Module_Info_Plugin.{cpp,h} is a
    # deprecated VST2 class whose only construction site is commented out at
    # ModuleFactory_Editor.cpp:1320 and whose class-type id 3 no switch handles;
    # and resource.h already exists in SynthEditLib, 361 lines in both copies
    # with all 318 ID_*/IDR_* constants identical (they differ only in the
    # APSTUDIO _APS_NEXT_RESOURCE_VALUE counter). That last one is only safe
    # while the constants stay identical and nothing enforces it -- BACKLOG P9.
    #
    # C12d (done) moved the last three -- InterfaceObject_editor.{cpp,h} and
    # platform_editor.cpp, 319 lines -- so this list now has ZERO ${EDITOR_DIR}
    # entries and C12 is complete. platform_editor.cpp is the editor-side
    # provider of new_InterfaceObjectA/B/C declared in SynthEditLib's
    # platform.h; its plugin-side counterpart platform_plugin.cpp was already
    # in SynthEditLib, so the two halves of that seam now sit together.
    #
    # C12d was marked linux on the theory that moving the provider would put it
    # "in the same archive as the code expecting it" and make the apps' GNU ld
    # rescan groups redundant. MEASURED, and the theory is wrong twice over.
    # C12 relocates files between REPOS, not between archives: these three are
    # still compiled into EditorLib, exactly as before, so the archive topology
    # is bit-for-bit unchanged and no rescan group can have become redundant
    # because of this stage. Separately, both SynthEditCL and SynthEditWayland
    # were measured linking WITHOUT their rescan group BEFORE this move (clean
    # relink, RC=0, zero undefined references) -- CMake already repeats both
    # archives on the link line, which satisfies the mutual reference the same
    # way --start-group does. The groups are therefore already redundant on
    # this toolchain, and that has nothing to do with C12d.
    #
    # The rescan groups are LEFT IN PLACE deliberately. "Links today" is not
    # "safe to remove": the repetition count is a CMake implementation detail,
    # the groups are explicit and cost nothing, and removing them is a change
    # with real risk and no benefit. Recorded here rather than acted on.
    ${SYNTHEDITLIB_DIR}/EditorLib/Application.h
    ${SYNTHEDITLIB_DIR}/EditorLib/Application.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/CContainer.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/CContainer.h
    ${SYNTHEDITLIB_DIR}/EditorLib/checkpoint.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/checkpoint.h
    ${SYNTHEDITLIB_DIR}/EditorLib/CLine2.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/CLine2.h
    ${SYNTHEDITLIB_DIR}/EditorLib/commandMgr.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/commandMgr.h
    ${SYNTHEDITLIB_DIR}/EditorLib/Control.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/Control.h
    ${SYNTHEDITLIB_DIR}/EditorLib/CPlugin.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/CPlugin.h
    ${SYNTHEDITLIB_DIR}/EditorLib/cpu_accumulator.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/cpu_accumulator.h
    ${SYNTHEDITLIB_DIR}/EditorLib/Dialogs_editor.h
    ${SYNTHEDITLIB_DIR}/EditorLib/Ctl_Combo.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/Ctl_Combo.h
    ${SYNTHEDITLIB_DIR}/EditorLib/Ctl_Keyboard2.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/Ctl_Keyboard2.h
    ${SYNTHEDITLIB_DIR}/EditorLib/Ctl_Slider.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/Ctl_Slider.h
    ${SYNTHEDITLIB_DIR}/EditorLib/Ctl_Text.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/Ctl_Text.h
    ${SYNTHEDITLIB_DIR}/EditorLib/CUG.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/CUG.h
    ${SYNTHEDITLIB_DIR}/EditorLib/CUG_with_patches.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/CUG_with_patches.h
    ${SYNTHEDITLIB_DIR}/EditorLib/DocOb.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/DocOb.h
    ${SYNTHEDITLIB_DIR}/EditorLib/FrameRateLogger.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/FrameRateLogger.h
    ${SYNTHEDITLIB_DIR}/EditorLib/FuzzyMatch.h
    ${SYNTHEDITLIB_DIR}/EditorLib/IGuiHost.h
    ${SYNTHEDITLIB_DIR}/EditorLib/imbedded_file.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/imbedded_file.h
    ${SYNTHEDITLIB_DIR}/EditorLib/InterfaceObject_editor.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/InterfaceObject_editor.h
    ${SYNTHEDITLIB_DIR}/EditorLib/it_doc_ob.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/it_doc_ob.h
    ${SYNTHEDITLIB_DIR}/EditorLib/it_doc_ob_recursive.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/it_doc_ob_recursive.h
    ${SYNTHEDITLIB_DIR}/EditorLib/it_plug_destinations.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/it_plug_destinations.h
    ${SYNTHEDITLIB_DIR}/EditorLib/legacyExternalApp.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/legacyExternalApp.h
    ${SYNTHEDITLIB_DIR}/EditorLib/MfcDocPresenter.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/MfcDocPresenter.h
    ${SYNTHEDITLIB_DIR}/EditorLib/ModuleBrowser.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/ModuleBrowser.h
    ${SYNTHEDITLIB_DIR}/EditorLib/PropertiesBrowser.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/PropertiesBrowser.h
    ${SYNTHEDITLIB_DIR}/EditorLib/ModuleDragAndDropManager.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/ModuleDragAndDropManager.h
    ${SYNTHEDITLIB_DIR}/EditorLib/ModuleFactory_Editor.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/ModuleFactory_Editor.h
    ${SYNTHEDITLIB_DIR}/EditorLib/PatchManager.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/PatchManager.h
    ${SYNTHEDITLIB_DIR}/EditorLib/PatchParameter.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/PatchParameter.h
    ${SYNTHEDITLIB_DIR}/EditorLib/PatchParameter_host_generated.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/PatchParameter_host_generated.h
    ${SYNTHEDITLIB_DIR}/EditorLib/platform_editor.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/Plug.h
    ${SYNTHEDITLIB_DIR}/EditorLib/plug4.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/Plug4.h
    ${SYNTHEDITLIB_DIR}/EditorLib/PlugDescriptionDecorator.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/PlugDescriptionDecorator.h
    ${SYNTHEDITLIB_DIR}/EditorLib/PlugIO4.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/PlugIO4.h
    ${SYNTHEDITLIB_DIR}/EditorLib/Plug_decorator_autoduplicate.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/Plug_decorator_autoduplicate.h
    ${SYNTHEDITLIB_DIR}/EditorLib/Plug_decorator_namable.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/Plug_decorator_namable.h
    ${SYNTHEDITLIB_DIR}/EditorLib/Plug_decorator_sdk2.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/Plug_decorator_sdk2.h
    ${SYNTHEDITLIB_DIR}/EditorLib/Plug_decorator_vst.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/Plug_decorator_vst.h
    ${SYNTHEDITLIB_DIR}/EditorLib/SkinMgr.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/SkinMgr.h
    ${SYNTHEDITLIB_DIR}/EditorLib/SuspendDSP.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/SuspendDSP.h
    ${SYNTHEDITLIB_DIR}/EditorLib/SynthEditDocBase.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/SynthEditDocBase.h
    # ExportAsPlugin.{cpp,h} are deliberately NOT here (carve-out stage C1b).
    # The export implementation is the commercial boundary, and this source list
    # is destined for the public repo -- so each exporting app compiles it
    # directly, exactly like SynthEditApp.cpp below: SynthEdit2.vcxproj, the
    # SynthEditMac Xcode project, SynthEditCL/CMakeLists.txt, and
    # SynthEditJuce/CMakeLists.txt (deprecated). Non-exporting consumers
    # (SynthEditSem/TIDE, EditorScreenshot, SynthEditWayland, tests) reference
    # only the declarations in CContainer.h and need nothing. Do not add the
    # .cpp to any library target -- executables only, or an app that links two
    # of them gets the definition twice.
    ${SYNTHEDITLIB_DIR}/EditorLib/UG2.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/UG2.h
    ${SYNTHEDITLIB_DIR}/EditorLib/ui_msg_target.h
    
    # SynthEdit2 source files
    ${SYNTHEDITLIB_DIR}/EditorLib/SynthEditDoc2.h
    ${SYNTHEDITLIB_DIR}/EditorLib/SynthEditDoc2.cpp

    # App-level sources shared across SynthEdit2, SynthEditCL, and SynthEditMac.
    # SynthEditApp.{h,cpp} are NOT here — each consuming app compiles them directly
    # so it can choose its own SE_MOONBASE_SUPPORT setting without ODR conflicts.
    ${SYNTHEDITLIB_DIR}/EditorLib/SynthEditAppBase.h
    ${SYNTHEDITLIB_DIR}/EditorLib/SynthEditAppBase.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/ApplySynthEditConfig.h
    ${SYNTHEDITLIB_DIR}/EditorLib/ApplySynthEditConfig.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/ThemeManager.h
    ${SYNTHEDITLIB_DIR}/EditorLib/ThemeManager.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/SynthRuntime_editor.h
    ${SYNTHEDITLIB_DIR}/EditorLib/SynthRuntime_editor.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/UIoManager.h
    ${SYNTHEDITLIB_DIR}/EditorLib/UIoManager.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/IO_base.h
    ${SYNTHEDITLIB_DIR}/EditorLib/IO_base.cpp
    ${SYNTHEDITLIB_DIR}/EditorLib/IO_None.h
    ${SYNTHEDITLIB_DIR}/EditorLib/IO_None.cpp
    ${SYNTHEDITLIB_DIR}/CancellationAnalyse.h
    ${SYNTHEDITLIB_DIR}/CancellationAnalyse.cpp
    ${SYNTHEDITLIB_DIR}/modules/se_sdk3_hosting/GmpiResourceManager_editor.h
    ${SYNTHEDITLIB_DIR}/modules/se_sdk3_hosting/GmpiResourceManager_editor.cpp
)

if(APPLE)
    # BACKLOG D3 — both TUs stay listed on every Apple target: they now pick
    # their own framework internally (TARGET_OS_OSX), so they compile for iOS
    # too and the live call sites (SkinMgr::EditSkin, MfcDocPresenter's
    # skin-folder command, Application's help/donation URLs) keep linking.
    # Excluding them on iOS instead would only turn a compile error into a
    # link error.
    target_sources(EditorLib PRIVATE
        ${GMPI_UI_SDK}/helpers/browseto.mm
        ${GMPI_UI_SDK}/helpers/openurl.mm
    )

    # AppKit is macOS-only; iOS wants UIKit. CoreText, CoreFoundation and
    # UniformTypeIdentifiers exist on both.
    if(IOS OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
        target_link_libraries(EditorLib PUBLIC "-framework UIKit" "-framework CoreText" "-framework CoreFoundation" "-framework UniformTypeIdentifiers")
    else()
        target_link_libraries(EditorLib PUBLIC "-framework AppKit" "-framework CoreText" "-framework CoreFoundation" "-framework UniformTypeIdentifiers")
    endif()
endif()

target_include_directories(EditorLib
    PUBLIC
        ${SYNTHEDITLIB_DIR}
        # C10: the carve-out's editor sources moved from the repo root into
        # EditorLib/. Consumers include them bare, so this dir propagates to
        # every CMake target that links EditorLib. Non-CMake consumers carry
        # their own copy of this line (SynthEdit2.vcxproj, SynthEditMac's
        # xcconfigs).
        ${SYNTHEDITLIB_DIR}/EditorLib
        ${VST3_SDK}
# ALL of the private SE16 directories that used to be listed here are gone, and
# as of C14 (2026-08-20) SE16 no longer re-adds one from its own root either.
# ../Shared does not exist in the tree at all and ../SynthEdit holds only icons
# and skins -- both measured dead by dropping them and rebuilding clean. The
# third, ../SynthEdit2, survived C13 for exactly one edge:
# ApplySynthEditConfig.cpp -> SynthEditApp.h. C14 closed it by narrowing both
# ApplyConfig* functions to CSynthEditAppBase, which is declared in THIS repo
# and already carries every member they use -- so nothing had to move, and the
# private licensing header never became a public dependency.
# EditorLib now compiles with ZERO private include directories, which is what
# C7's clean-clone test needs. Do not re-add one: if a public file needs a
# private declaration, narrow it to a public base or interface instead
# (C11 and C14 are the two precedents).
)

# EditorLib calls into SynthEditLib; declaring the edge makes CMake order the
# static archives correctly for GNU ld (which resolves left-to-right).
target_link_libraries(EditorLib PUBLIC SynthEditLib)

# Add compile definitions needed for WinUI3/SynthEdit2 compatibility
target_compile_definitions(EditorLib
    PUBLIC
        _CRT_SECURE_NO_WARNINGS
        _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
        SE_WINUI3
        SE_EDIT_SUPPORT
        SE_USE_LIB_FACTORY
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

# SE_MOONBASE_SUPPORT is intentionally not defined here. Moonbase (licensing)
# lives in SynthEditApp.cpp, which is compiled directly by each consuming app
# (SynthEditMac Xcode project, SynthEdit2.vcxproj, SynthEditCL CMakeLists.txt).

# Carve-out C9 / C6: SE_APP_BUILD_NUMBER is NOT injected here any more.
# ModuleFactory_Editor.cpp, SkinMgr.cpp and Application.cpp include
# SynthEditLib/se_version.h, which defaults it to 0 -- "this application never
# invalidates its caches on upgrade". The real value comes from SE16's private
# se_build_number.h, which stays exactly where SynthEdit's three release
# workflows grep for it, so the injection now lives in SE16's root CMakeLists
# immediately after this directory is added. A clean clone of this repo, with
# no access to SE16, gets the 0 default -- which is what C7 wants and what TIDE
# wants anyway.
#
# NOTE this deliberately differs from what the pre-C6 comment here predicted.
# It said the injection "belongs in each SynthEdit application's own build".
# It cannot: SE_APP_BUILD_NUMBER is PRIVATE to EditorLib, so it is baked in when
# EditorLib's own TUs compile, and this tree builds ONE EditorLib shared by
# SynthEditCL, SynthEditWayland, SynthEdit2 and TIDE alike. A per-application
# definition cannot reach those TUs, and per-application EditorLib targets would
# be a real change in behaviour rather than a relocation. Keeping a single
# injection at the target keeps every consumer on exactly the number it saw
# before C6 -- including TIDE, as se_version.h already documents.

set_property(TARGET EditorLib PROPERTY CXX_STANDARD 20)

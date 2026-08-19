cmake_minimum_required(VERSION 3.30)

project(SampleOscillator2)

build_gmpi_plugin(
    PROJECT_NAME ${PROJECT_NAME}
    IS_OFFICIAL_MODULE
    HAS_GUI
    HAS_DSP
    SOURCE_FILES 
        SampleOscillator2.h
        SampleOscillator2.cpp
        SampleLoader2.h
        SampleLoader2.cpp
        SampleLoader2Gui.h
        SampleLoader2Gui.cpp
        SampleManager.h
        SampleManager.cpp
        SampleExclusiveFilter.h
        SampleExclusiveFilter.cpp
        csoundfont.h
        CSoundFont.cpp
        soundfont_user.h
        soundfont_user.cpp
        RiffFile2.h
        RiffFile2.cpp
)

# S19. The tree-wide Apple flags in SynthEdit's CMakeLists include
# -fassociative-math and -freciprocal-math. Reassociation changes this module's
# output enough to fail TestSoundfont.SoundfontOsc against its baked reference:
# measured 2026-08-20 on macOS, the test PASSES with those two flags removed and
# FAILS with them, everything else unchanged.
#
# Scoped to this module rather than dropped tree-wide, because the flags are
# wanted everywhere else -- and per-source rather than per-target because
# build_gmpi_plugin() creates the targets and their names are its business, not
# ours. These land after CMAKE_CXX_FLAGS on the command line, so they win.
if(APPLE)
    set_source_files_properties(
        SampleOscillator2.cpp
        SampleLoader2.cpp
        SampleManager.cpp
        SampleExclusiveFilter.cpp
        CSoundFont.cpp
        soundfont_user.cpp
        RiffFile2.cpp
        PROPERTIES COMPILE_OPTIONS "-fno-associative-math;-fno-reciprocal-math"
    )
endif()

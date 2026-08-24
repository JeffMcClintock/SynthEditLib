cmake_minimum_required(VERSION 3.30)

project(Soundpipe)

# BACKLOG S18. This is the one place in the public repo that needs a
# third-party SDK living outside it. `external_sdk_folder` is set ONLY by SE16
# (SynthEdit/CMakeLists.txt sets it to its own SDKs/ folder), and
# SynthEditLib/modules is added by SE16's root, never by SynthEditLib's own --
# so a clean clone of the public repo that adds modules/ fails with
#
#     Cannot find source file: /Soundpipe/modules/base.c
#     No SOURCES given to target: Soundpipe
#
# which says nothing about WHICH sdk is missing or where to get it. Skip the
# target and explain, rather than failing on a path that means nothing.
#
# Vendoring or fetching Soundpipe publicly is the other option S18 offers and
# would let this build standalone. That is a licensing question first --
# Soundpipe is third-party -- so it is deliberately NOT decided here.
if(NOT external_sdk_folder OR NOT EXISTS "${external_sdk_folder}/Soundpipe/soundpipe.h")
    message(STATUS
        "Soundpipe modules SKIPPED: the Soundpipe SDK was not found. Set "
        "external_sdk_folder to a folder containing Soundpipe/ (SE16 sets it to "
        "its own SDKs/). See BACKLOG S18.")
    return()
endif()
#..\se_sdk3;..\..\..\SDKs\Soundpipe\h\;..\..\..\SDKs\Soundpipe\;..\..\..\SDKs\Soundpipe\lib\dr_wav\
#;..\..\..\SDKs\Soundpipe\lib\faust\;%(AdditionalIncludeDirectories)
include_directories(
    ${external_sdk_folder}/Soundpipe
    ${external_sdk_folder}/Soundpipe/h
    ${external_sdk_folder}/Soundpipe/lib/dr_wav
    ${external_sdk_folder}/Soundpipe/lib/faust
)

set(soundpipe_srcs
    ${external_sdk_folder}/Soundpipe/modules/base.c
    ${external_sdk_folder}/Soundpipe/modules/jcrev.c
    ${external_sdk_folder}/Soundpipe/modules/revsc.c
    ${external_sdk_folder}/Soundpipe/modules/zitarev.c
    ${external_sdk_folder}/Soundpipe/h/revsc.h
    ${external_sdk_folder}/Soundpipe/h/zitarev.h
)

build_gmpi_plugin(
    PROJECT_NAME ${PROJECT_NAME}
    IS_OFFICIAL_MODULE
    HAS_DSP
    HAS_GUI
    SOURCE_FILES 
        ReverbChowning.cpp
        ReverbZita.cpp
        ReverbSp.cpp
        ${soundpipe_srcs}
)

source_group(Soundpipe FILES ${soundpipe_srcs})

add_definitions(-DNO_LIBSNDFILE)


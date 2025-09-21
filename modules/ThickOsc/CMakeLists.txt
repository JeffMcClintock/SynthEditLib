cmake_minimum_required(VERSION 3.30)

project(ThickOscillator)

set(HAS_DSP TRUE)


if(HAS_DSP)
set(srcs ${srcs}
${PROJECT_NAME}.cpp
)
endif()

if(HAS_GUI)
set(srcs ${srcs}
${PROJECT_NAME}Gui.cpp
)
endif()

set(sdk_srcs
${sdk_folder}/mp_sdk_common.h
${sdk_folder}/mp_sdk_common.cpp
)

if(HAS_DSP)
set(sdk_srcs ${sdk_srcs}
${sdk_folder}/mp_sdk_audio.h
${sdk_folder}/mp_sdk_audio.cpp
)
endif()

if(HAS_GUI)
set(sdk_srcs ${sdk_srcs}
${sdk_folder}/mp_sdk_gui.h
${sdk_folder}/mp_sdk_gui.cpp
)
endif()

set(resource_srcs
module.rc
${PROJECT_NAME}.xml
)

include_directories(
${sdk_folder}
)

source_group(sdk FILES ${sdk_srcs})
source_group(resources FILES ${resource_srcs})

include (GenerateExportHeader)
add_library(${PROJECT_NAME} MODULE  ${srcs} ${sdk_srcs} ${resource_srcs})

if(APPLE)
  set_target_properties(${PROJECT_NAME} PROPERTIES BUNDLE TRUE)
  set_target_properties(${PROJECT_NAME} PROPERTIES BUNDLE_EXTENSION "sem")

  # Place xml file in bundle 'Resources' folder.
  set(xml_path "${PROJECT_NAME}.xml")
  target_sources(${PROJECT_NAME} PUBLIC ${xml_path})
  set_source_files_properties(${xml_path} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "MSVC")
  set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".sem")

if(CMAKE_HOST_WIN32)
add_custom_command(TARGET ${PROJECT_NAME}
    # Run after all other rules within the target have been executed
    POST_BUILD
    COMMAND xcopy /c /y "$(OutDir)$(TargetName)$(TargetExt)" "C:\\Program Files\\Common Files\\SynthEdit\\modules"
    COMMENT "Copy to system plugin folder"
    VERBATIM
)
endif()

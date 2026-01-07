# include the standard source code files in the plugin, includes the sdk and plugin itself
# DSP and Gui and XML file names are generated from the project name
macro(
    set_plugin_standard_srcs
    PROJECT_NAME
    HAS_DSP
    HAS_GUI
)

set(sdk_srcs
${se_sdk_folder}/mp_sdk_common.h
${se_sdk_folder}/mp_sdk_common.cpp
)

if(${HAS_DSP})
    set(sdk_srcs ${sdk_srcs}
    ${se_sdk_folder}/mp_sdk_audio.h
    ${se_sdk_folder}/mp_sdk_audio.cpp
    )
    set(srcs ${srcs}
    ${PROJECT_NAME}.cpp
    )
endif()

if(${HAS_GUI})
    set(sdk_srcs ${sdk_srcs}
    ${se_sdk_folder}/mp_sdk_gui.h
    ${se_sdk_folder}/mp_sdk_gui.cpp
    )
    set(srcs ${srcs}
    ${PROJECT_NAME}Gui.cpp
    )
endif()

set(resource_srcs
    ${PROJECT_NAME}.xml
)

if(CMAKE_HOST_WIN32)
set(resource_srcs
    ${resource_srcs}
    module.rc
)
endif()

# organise SDK file into folders/groups in IDE
source_group(sdk FILES ${sdk_srcs})
source_group(resources FILES ${resource_srcs})

endmacro()

# include the standard source code files in the plugin, includes the sdk and plugin itself
# DSP and Gui and XML file names are generated from the project name
macro(
    build_sem_plugin
    PROJECT_NAME
    HAS_DSP
    HAS_GUI
)

project(${PROJECT_NAME})

set_plugin_standard_srcs(
    ${PROJECT_NAME}
    ${HAS_DSP}
    ${HAS_GUI}
)

include (GenerateExportHeader)
add_library(${PROJECT_NAME} MODULE ${srcs} ${sdk_srcs} ${resource_srcs})

target_compile_definitions(${PROJECT_NAME} PRIVATE 
  $<$<CONFIG:Debug>:_DEBUG>
  $<$<CONFIG:Release>:NDEBUG>
)

if(APPLE)
  set_target_properties(${PROJECT_NAME} PROPERTIES BUNDLE TRUE)
  set_target_properties(${PROJECT_NAME} PROPERTIES BUNDLE_EXTENSION "sem")

  # generate debug symbols
  target_compile_options(${PROJECT_NAME} PRIVATE
      $<$<CONFIG:Debug>:-g>
  )
  set_target_properties(${PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT
    $<$<CONFIG:Debug>:dwarf-with-dsym>
  )
  
  # Place xml file in bundle 'Resources' folder.
  set(xml_path "${PROJECT_NAME}.xml")
  target_sources(${PROJECT_NAME} PUBLIC ${xml_path})
  set_source_files_properties(${xml_path} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "MSVC")
  set_property(TARGET ${PROJECT_NAME} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".sem")

if(WIN32)
target_link_options(${PROJECT_NAME} PRIVATE "/SUBSYSTEM:WINDOWS")
endif()

if(CMAKE_HOST_WIN32)

if (SE_COPY_TO_SEM_FOLDER)
    add_custom_command(TARGET ${PROJECT_NAME}
    # Run after all other rules within the target have been executed
    POST_BUILD
    COMMAND xcopy /c /y "$(OutDir)$(TargetName)$(TargetExt)" "C:\\Program Files\\Common Files\\SynthEdit\\modules"
    COMMENT "Copy to system plugin folder"
    VERBATIM
)
endif()
endif()

# all individual modules should be groups under "modules" solution folder
SET_TARGET_PROPERTIES(${PROJECT_NAME} PROPERTIES FOLDER "modules")

endmacro()

#more sophisticated
function(BUILD_GMPI_PLUGIN)
set(options HAS_DSP HAS_GUI IS_OFFICIAL_MODULE)
set(oneValueArgs PROJECT_NAME)
set(multiValueArgs SOURCE_FILES)
cmake_parse_arguments(BUILD_GMPI_PLUGIN "${options}" "${oneValueArgs}"
                          "${multiValueArgs}" ${ARGN} )

# message(STATUS "PROJECT_NAME:" ${BUILD_GMPI_PLUGIN_PROJECT_NAME})

# add SDK files
set(sdk_srcs
${se_sdk_folder}/mp_sdk_common.h
${se_sdk_folder}/mp_sdk_common.cpp
)

if(${BUILD_GMPI_PLUGIN_HAS_DSP})
    set(sdk_srcs ${sdk_srcs}
    ${se_sdk_folder}/mp_sdk_audio.h
    ${se_sdk_folder}/mp_sdk_audio.cpp
    )
    set(srcs ${srcs}
    ${BUILD_GMPI_PLUGIN_PROJECT_NAME}.cpp
    )
endif()

if(${BUILD_GMPI_PLUGIN_HAS_GUI})
    set(sdk_srcs ${sdk_srcs}
    ${se_sdk_folder}/mp_sdk_gui.h
    ${se_sdk_folder}/mp_sdk_gui2.h
    ${se_sdk_folder}/mp_sdk_gui.cpp
    )
    set(srcs ${srcs}
    ${BUILD_GMPI_PLUGIN_PROJECT_NAME}Gui.cpp
    )
endif()

set(resource_srcs
${BUILD_GMPI_PLUGIN_PROJECT_NAME}.xml
)

if(CMAKE_HOST_WIN32)
set(resource_srcs
    ${resource_srcs}
    ${BUILD_GMPI_PLUGIN_PROJECT_NAME}.rc
)
endif()

# organise SDK file into folders/groups in IDE
source_group(sdk FILES ${sdk_srcs})
source_group(resources FILES ${resource_srcs})

include (GenerateExportHeader)
add_library(${BUILD_GMPI_PLUGIN_PROJECT_NAME} MODULE ${BUILD_GMPI_PLUGIN_SOURCE_FILES} ${sdk_srcs} ${resource_srcs})

target_compile_definitions(
  ${BUILD_GMPI_PLUGIN_PROJECT_NAME} PRIVATE 
  $<$<CONFIG:Debug>:_DEBUG>
  $<$<CONFIG:Release>:NDEBUG>
)

if(APPLE)
  set_target_properties(${BUILD_GMPI_PLUGIN_PROJECT_NAME} PROPERTIES BUNDLE TRUE)
  set_target_properties(${BUILD_GMPI_PLUGIN_PROJECT_NAME} PROPERTIES BUNDLE_EXTENSION "sem")

  # generate debug symbols
  target_compile_options(${BUILD_GMPI_PLUGIN_PROJECT_NAME} PRIVATE
      $<$<CONFIG:Debug>:-g>
      -Wno-deprecated-declarations
  )
  set_target_properties(${BUILD_GMPI_PLUGIN_PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT
    $<$<CONFIG:Debug>:dwarf-with-dsym>
  )

  # Place xml file in bundle 'Resources' folder.
  set(xml_path "${BUILD_GMPI_PLUGIN_PROJECT_NAME}.xml")
  target_sources(${BUILD_GMPI_PLUGIN_PROJECT_NAME} PUBLIC ${xml_path})
  set_source_files_properties(${xml_path} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

set_target_properties(${BUILD_GMPI_PLUGIN_PROJECT_NAME} PROPERTIES SUFFIX ".sem")

if(WIN32)
target_compile_definitions (${BUILD_GMPI_PLUGIN_PROJECT_NAME} PRIVATE -D_UNICODE -DUNICODE)
target_link_options(${BUILD_GMPI_PLUGIN_PROJECT_NAME} PRIVATE "/SUBSYSTEM:WINDOWS")
target_compile_definitions (${BUILD_GMPI_PLUGIN_PROJECT_NAME} PRIVATE -D_UNICODE -DUNICODE)
endif()

if(CMAKE_HOST_WIN32)
if (SE_LOCAL_BUILD)
if(${BUILD_GMPI_PLUGIN_IS_OFFICIAL_MODULE})
    add_custom_command(TARGET ${BUILD_GMPI_PLUGIN_PROJECT_NAME}
    # Run after all other rules within the target have been executed
    POST_BUILD
    COMMAND xcopy /c /y "\"$(OutDir)$(TargetName)$(TargetExt)\"" "\"C:\\SE\\SE16\\SynthEdit2\\mac_assets\\$(TargetName)$(TargetExt)\\Contents\\x86_64-win\\\""
    COMMENT "Copy to SynthEdit plugin folder"
    )
else()
    add_custom_command(TARGET ${BUILD_GMPI_PLUGIN_PROJECT_NAME}
    # Run after all other rules within the target have been executed
    POST_BUILD
    COMMAND xcopy /c /y "\"$(OutDir)$(TargetName)$(TargetExt)\"" "\"C:\\Program Files\\Common Files\\SynthEdit\\modules\\\""
    COMMENT "Copy to SynthEdit plugin folder"
    )
endif()
endif()
endif()

# all individual modules should be groups under "modules" solution folder
SET_TARGET_PROPERTIES(${BUILD_GMPI_PLUGIN_PROJECT_NAME} PROPERTIES FOLDER "modules")

endfunction()

# GMPI 2.0 plugin. copied from GMPI SDK but without the wrappers
function(synthedit_plugin)
    set(options HAS_DSP HAS_GUI HAS_XML IS_OFFICIAL_MODULE)
    set(oneValueArgs PROJECT_NAME)
    set(multiValueArgs SOURCE_FILES)
    cmake_parse_arguments(GMPI_PLUGIN "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT GMPI_PLUGIN_PROJECT_NAME)
        message(FATAL_ERROR "gmpi_plugin(PROJECT_NAME <name>) is required.")
    endif()

    # Default to GMPI if no formats were specified.
    if(NOT GMPI_PLUGIN_FORMATS_LIST)
        set(GMPI_PLUGIN_FORMATS_LIST GMPI)
    endif()

    # add SDK files
    set(plugin_includes
        ${GMPI_SDK}
        ${GMPI_SDK}/Core
    )
    
    set(sdk_srcs
        ${GMPI_SDK}/Core/Common.h
        ${GMPI_SDK}/Core/Common.cpp
        ${GMPI_SDK}/Core/RefCountMacros.h
        ${GMPI_SDK}/Core/GmpiApiCommon.h
        ${GMPI_SDK}/Core/GmpiSdkCommon.h
    )

    if(GMPI_PLUGIN_HAS_DSP)
        list(APPEND sdk_srcs
            ${GMPI_SDK}/Core/Processor.h
            ${GMPI_SDK}/Core/Processor.cpp
            ${GMPI_SDK}/Core/GmpiApiAudio.h
        )
    endif()

    if(GMPI_PLUGIN_HAS_GUI)
        list(APPEND sdk_srcs
            ${GMPI_UI_SDK}/GmpiApiDrawing.h
            ${GMPI_SDK}/Core/GmpiApiEditor.h
            ${GMPI_UI_SDK}/Drawing.h
        )
        list(APPEND plugin_includes ${GMPI_UI_SDK})
    endif()

    if(GMPI_PLUGIN_HAS_XML)
        set(resource_srcs
            ${GMPI_PLUGIN_PROJECT_NAME}.xml
        )

        if(WIN32)
            list(APPEND resource_srcs ${GMPI_PLUGIN_PROJECT_NAME}.rc)
            source_group(resources FILES ${resource_srcs})
        endif()
    endif()

    foreach(kind IN LISTS GMPI_PLUGIN_FORMATS_LIST)
        if(kind STREQUAL "GMPI")
            set(SUB_PROJECT_NAME ${GMPI_PLUGIN_PROJECT_NAME})
        else()
            set(SUB_PROJECT_NAME ${GMPI_PLUGIN_PROJECT_NAME}_${kind})
        endif()

        set(FORMAT_SDK_FILES ${sdk_srcs})

        # Organize SDK files in IDE
        source_group(sdk FILES ${FORMAT_SDK_FILES})

        add_library(${SUB_PROJECT_NAME} MODULE
            ${GMPI_PLUGIN_SOURCE_FILES}
            ${FORMAT_SDK_FILES}
            ${resource_srcs}
            ${wrapper_srcs}
        )

        # Target-based includes
        if(plugin_includes)
            target_include_directories(${SUB_PROJECT_NAME} PRIVATE ${plugin_includes})
        endif()

#        synthedit_target(PROJECT_NAME ${SUB_PROJECT_NAME})
#        function(synthedit_target)
#    set(options)
#    set(oneValueArgs PROJECT_NAME)
#    set(multiValueArgs)
#    cmake_parse_arguments(GMPI_TARGET "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

#    if(NOT GMPI_TARGET_PROJECT_NAME)
#        message(FATAL_ERROR "gmpi_target(PROJECT_NAME <name>) is required.")
#    endif()

    # Use target-based definitions and features
    target_compile_definitions(
        ${SUB_PROJECT_NAME} PRIVATE
        $<$<CONFIG:Debug>:_DEBUG>
        $<$<CONFIG:Release>:NDEBUG>
    )
    # Workspace uses C++14
    target_compile_features(${SUB_PROJECT_NAME} PUBLIC cxx_std_17)

    if(APPLE)
        # Guard Apple-specific frameworks
        target_link_libraries(${SUB_PROJECT_NAME} PRIVATE
            ${COREFOUNDATION_LIBRARY}
            ${COCOA_LIBRARY}
            ${CORETEXT_LIBRARY}
            ${OPENGL_LIBRARY}
            ${IMAGEIO_LIBRARY}
            ${COREGRAPHICS_LIBRARY}
            ${ACCELERATE_LIBRARY}
            ${QUARTZCORE_LIBRARY}
        )
    endif()

    if(WIN32)
        # Prefer bare names; MSVC resolves .lib automatically
        target_link_libraries(${SUB_PROJECT_NAME} PRIVATE d3d11 d2d1 dwrite windowscodecs)
        target_link_options(${SUB_PROJECT_NAME} PRIVATE "/SUBSYSTEM:WINDOWS")
    endif()
#endfunction()     

#        set(TARGET_EXTENSION "${kind}")
        string(TOLOWER "${kind}" TARGET_EXTENSION)

        if(APPLE)
            set_target_properties(${SUB_PROJECT_NAME}
            PROPERTIES
                BUNDLE TRUE
                BUNDLE_EXTENSION "${TARGET_EXTENSION}"
        )
        else()
            set_target_properties(${SUB_PROJECT_NAME}
                PROPERTIES
                SUFFIX ".${TARGET_EXTENSION}"
            )
        endif()
    endforeach()

    list(FIND GMPI_PLUGIN_FORMATS_LIST "GMPI" FIND_GMPI_INDEX)


    # Group modules under solution folders
    if(FIND_GMPI_INDEX GREATER_EQUAL 0)
        set_target_properties(${GMPI_PLUGIN_PROJECT_NAME} PROPERTIES FOLDER "modules")
    endif()

if(CMAKE_HOST_WIN32)
if (SE_LOCAL_BUILD)
if(${BUILD_GMPI_PLUGIN_IS_OFFICIAL_MODULE})
    add_custom_command(TARGET ${GMPI_PLUGIN_PROJECT_NAME}
    # Run after all other rules within the target have been executed
    POST_BUILD
    COMMAND xcopy /c /y "\"$(OutDir)$(TargetName)$(TargetExt)\"" "\"C:\\SE\\SE16\\SynthEdit2\\mac_assets\\$(TargetName)$(TargetExt)\\Contents\\x86_64-win\\\""
    COMMENT "Copy to SynthEdit plugin folder"
    )
else()
    add_custom_command(TARGET ${GMPI_PLUGIN_PROJECT_NAME}
    # Run after all other rules within the target have been executed
    POST_BUILD
    COMMAND xcopy /c /y "\"$(OutDir)$(TargetName)$(TargetExt)\"" "\"C:\\Program Files\\Common Files\\SynthEdit\\modules\\\""
    COMMENT "Copy to SynthEdit plugin folder"
    )
endif()
endif()
endif()

endfunction()

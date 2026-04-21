cmake_minimum_required(VERSION 3.30)

project(SubControls4)

set(TINYXML2_PARENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(TINYXML2_DIR "${TINYXML2_PARENT_DIR}/tinyXml2")

gmpi_plugin(
    PROJECT_NAME ${PROJECT_NAME}
    HAS_GUI
    IS_OFFICIAL_MODULE
    FORMATS_LIST GMPI
    SOURCE_FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/../PluginUIScale/PluginUIScaleGui.cpp
        GmpiUiTest.cpp
        DialogGui.cpp
        DirectoryDialogGui.cpp
        ${GMPI_UI_SDK}/helpers/Timer.h
        ${GMPI_UI_SDK}/helpers/Timer.cpp
        ${GMPI_UI_SDK}/helpers/GinBlur.h
        ${GMPI_UI_SDK}/helpers/CachedBlur.h
        ${GMPI_UI_SDK}/helpers/ImageCache.h
        ${GMPI_UI_SDK}/helpers/ImageCache.cpp
        ${GMPI_UI_SDK}/helpers/ImageMetadata.h
        ${GMPI_UI_SDK}/helpers/ImageMetadata.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/simdutf/simdutf.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/simdutf/simdutf.cpp
        ${TINYXML2_DIR}/tinyxml2.h
        ${TINYXML2_DIR}/tinyxml2.cpp
)

# Include parent dir so #include "tinyXml2/tinyxml2.h" works (used by SvgParser.h)
target_include_directories(${PROJECT_NAME} PRIVATE "${TINYXML2_PARENT_DIR}")

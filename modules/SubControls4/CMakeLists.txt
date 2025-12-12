cmake_minimum_required(VERSION 3.30)

project(SubControls4)

gmpi_plugin(
    PROJECT_NAME ${PROJECT_NAME}
    HAS_GUI
    IS_OFFICIAL_MODULE
    FORMATS_LIST GMPI
    SOURCE_FILES 
        GmpiUiTest.cpp
        ${GMPI_UI_SDK}/helpers/Timer.h
        ${GMPI_UI_SDK}/helpers/Timer.cpp
        ${GMPI_UI_SDK}/helpers/GinBlur.h
        ${GMPI_UI_SDK}/helpers/CachedBlur.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/simdutf/simdutf.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../shared/simdutf/simdutf.cpp
)

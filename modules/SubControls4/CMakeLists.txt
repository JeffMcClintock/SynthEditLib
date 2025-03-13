cmake_minimum_required(VERSION 3.15)

project(SubControls4)

gmpi_plugin(
    PROJECT_NAME ${PROJECT_NAME}
    HAS_GUI
    IS_OFFICIAL_MODULE
    FORMATS_LIST GMPI
    SOURCE_FILES 
        GmpiUiTest.cpp
        ${gmpi_ui_folder}/helpers/Timer.h
        ${gmpi_ui_folder}/helpers/Timer.cpp
)

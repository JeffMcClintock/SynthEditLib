cmake_minimum_required(VERSION 3.15)

project(SubControls4)

gmpi_plugin(
    PROJECT_NAME ${PROJECT_NAME}
    HAS_GUI
    FORMATS_LIST GMPI
    SOURCE_FILES 
        GmpiUiTest.cpp
)

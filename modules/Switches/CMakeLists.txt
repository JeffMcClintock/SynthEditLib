cmake_minimum_required(VERSION 3.15)

project(Switches)

build_gmpi_plugin(
    PROJECT_NAME ${PROJECT_NAME}
    IS_OFFICIAL_MODULE
    HAS_GUI
    HAS_DSP
    SOURCE_FILES 
        Switches.h
        Switches.cpp
        SwitchesGui.h
        SwitchesGui.cpp
        SilenceGate.cpp
)

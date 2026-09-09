cmake_minimum_required(VERSION 3.30)

project(Diagnostics2)

gmpi_plugin(
    PROJECT_NAME ${PROJECT_NAME}
    HAS_GUI IS_OFFICIAL_MODULE
    FORMATS_LIST GMPI
    SOURCE_FILES
        ParametersQueryGui.cpp
)

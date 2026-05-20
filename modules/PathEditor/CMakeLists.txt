cmake_minimum_required(VERSION 3.30)

project(PathEditor)

gmpi_plugin(
    PROJECT_NAME ${PROJECT_NAME}
    HAS_GUI
    FORMATS_LIST GMPI
    SOURCE_FILES
        PathEditorGui.cpp
        PathRenderGui.cpp
)

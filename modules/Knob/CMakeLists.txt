cmake_minimum_required(VERSION 3.30)

project(Knob)

gmpi_plugin(
	PROJECT_NAME ${PROJECT_NAME}
	HAS_GUI
	SOURCE_FILES
	  KnobGui.cpp
)

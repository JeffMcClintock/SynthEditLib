cmake_minimum_required(VERSION 3.30)

project(Bump)

gmpi_plugin(
	PROJECT_NAME ${PROJECT_NAME}
	HAS_GUI
	SOURCE_FILES
	  BumpGui.cpp
)

cmake_minimum_required(VERSION 3.30)

project(Logic)

gmpi_plugin(
	PROJECT_NAME ${PROJECT_NAME}
	HAS_DSP
 FORMATS_LIST GMPI
	SOURCE_FILES
	  Gates.cpp
)

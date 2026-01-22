cmake_minimum_required(VERSION 3.30)

project(Converters2)

gmpi_plugin(
	PROJECT_NAME ${PROJECT_NAME}
	HAS_DSP
	SOURCE_FILES
	  Converters2.cpp
)

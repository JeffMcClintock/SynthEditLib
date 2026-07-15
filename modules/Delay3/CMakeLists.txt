cmake_minimum_required(VERSION 3.30)

project(Delay3)

gmpi_plugin(
	PROJECT_NAME ${PROJECT_NAME}
	HAS_DSP
	SOURCE_FILES
	  Delay3.cpp
)

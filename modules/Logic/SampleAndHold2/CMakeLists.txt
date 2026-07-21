cmake_minimum_required(VERSION 3.30)

project(SampleAndHold2)

gmpi_plugin(
	PROJECT_NAME ${PROJECT_NAME}
	HAS_DSP
	SOURCE_FILES
	  SampleAndHold2.cpp
)

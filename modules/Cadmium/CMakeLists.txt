cmake_minimum_required(VERSION 3.30)

project(Cadmium)

synthedit_plugin(
	PROJECT_NAME ${PROJECT_NAME}
	HAS_DSP IS_OFFICIAL_MODULE
	SOURCE_FILES
	  Blob2Test.cpp
)

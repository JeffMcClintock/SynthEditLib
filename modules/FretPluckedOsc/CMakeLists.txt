cmake_minimum_required(VERSION 3.30)

project(FretPluckedOsc)

synthedit_plugin(
	PROJECT_NAME ${PROJECT_NAME}
	HAS_DSP IS_OFFICIAL_MODULE
	SOURCE_FILES
	  FretPluckedOsc.h
	  FretPluckedOsc.cpp
	  FretPluckedBake.h
	  FretPluckedBake.cpp
	  FretPluckedCache.h
	  FretPluckedCache.cpp
)

cmake_minimum_required(VERSION 3.30)

project(FloatToVolts2)

synthedit_plugin(
	PROJECT_NAME ${PROJECT_NAME}
	HAS_DSP IS_OFFICIAL_MODULE
	SOURCE_FILES
	  FloatToVolts2.cpp
)

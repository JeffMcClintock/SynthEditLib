cmake_minimum_required(VERSION 3.30)

project(FloatToVolts2)

# IS_OFFICIAL_MODULE

synthedit_plugin(
	PROJECT_NAME ${PROJECT_NAME}
	HAS_DSP 
	SOURCE_FILES
	  FloatToVolts2.cpp
)

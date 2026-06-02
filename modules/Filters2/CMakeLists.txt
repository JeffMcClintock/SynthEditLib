cmake_minimum_required(VERSION 3.30)

project(LowPass6dB)

gmpi_plugin(
	PROJECT_NAME ${PROJECT_NAME}
	HAS_DSP
	SOURCE_FILES
	  LowPass6dB.cpp
	  ${GMPI_SDK}/Helpers/FilterBase.h
)

cmake_minimum_required(VERSION 3.30)

project(Oscillator)

set(SDK_FILES
	../shared/real_fft.cpp
)

synthedit_plugin(
	PROJECT_NAME ${PROJECT_NAME}
	HAS_DSP IS_OFFICIAL_MODULE
	SOURCE_FILES
	  Oscillator.cpp
	  OscMipmaps.h
	  ${SDK_FILES}
)

source_group("sdk" FILES ${SDK_FILES})

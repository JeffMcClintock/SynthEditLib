cmake_minimum_required(VERSION 3.30)

project(LowPass6dB)

gmpi_plugin(
	PROJECT_NAME ${PROJECT_NAME}
	HAS_DSP
	SOURCE_FILES
	  LowPass6dB.cpp
	  LowPass6dB_stereo.cpp
	  ${GMPI_SDK}/Helpers/FilterBase.h
)

gmpi_plugin(
	PROJECT_NAME SvfTpt
	HAS_DSP
	SOURCE_FILES
	  SvfTpt.cpp
	  ${GMPI_SDK}/Helpers/FilterBase.h
)

gmpi_plugin(
	PROJECT_NAME OnePoleTpt
	HAS_DSP
	SOURCE_FILES
	  LowPassTpt.cpp
	  HighPassTpt.cpp
	  OnePoleTpt.h
	  ${GMPI_SDK}/Helpers/FilterBase.h
)

gmpi_plugin(
	PROJECT_NAME SvfEq
	HAS_DSP
	SOURCE_FILES
	  LowShelfTpt.cpp
	  HighShelfTpt.cpp
	  BellTpt.cpp
	  SvfEq.h
	  ${GMPI_SDK}/Helpers/FilterBase.h
)

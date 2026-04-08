cmake_minimum_required(VERSION 3.30)

project(WavetableOsc)

set(SDK_FILES
	../shared/real_fft.cpp
	../shared/it_enum_list.cpp
	../shared/FileFinder.cpp
	${GMPI_UI_SDK}/helpers/Timer.cpp
)

synthedit_plugin(
	PROJECT_NAME ${PROJECT_NAME}
	HAS_DSP HAS_GUI IS_OFFICIAL_MODULE
	SOURCE_FILES
	  WavetableOsc.cpp
	  WavetableOscGui.cpp
	  Wavetable.cpp
	  WaveTableLoad.cpp
	  WavetableMipmapPolicy.h
	  ${SDK_FILES}
)

source_group("sdk" FILES ${SDK_FILES})


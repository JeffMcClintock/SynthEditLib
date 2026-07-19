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
	HAS_DSP HAS_GUI IS_OFFICIAL_MODULE USE_STAGING
	SOURCE_FILES
	  WavetableOsc.h
	  WavetableOsc.cpp
	  WavetableOscGui.h
	  WavetableOscGui.cpp
	  Wavetable.h
	  Wavetable.cpp
	  WavetableCache.h
	  WavetableCache.cpp
	  WavetableLoader.h
	  WavetableLoader.cpp
	  WavetableMipmapPolicy.h
	  ${SDK_FILES}
)

source_group("sdk" FILES ${SDK_FILES})


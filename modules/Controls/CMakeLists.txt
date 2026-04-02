cmake_minimum_required(VERSION 3.30)

project(Controls)

gmpi_plugin(
	PROJECT_NAME ${PROJECT_NAME}
	HAS_GUI
	SOURCE_FILES
	  KnobGui.cpp
	  ButtonGui.cpp
	  SliderGui.cpp
	  HSliderGui.cpp
	  LEDGui.cpp
	  BumpGui.cpp
	  PanelGroupGui.cpp
	  PlainImageGui.cpp
	  ${GMPI_UI_SDK}/helpers/ImageCache.cpp
	  ${GMPI_UI_SDK}/helpers/ImageMetadata.cpp
	  VUMeterGui.cpp
	  BarLEDMeterGui.cpp
	  PostItGui.cpp
)

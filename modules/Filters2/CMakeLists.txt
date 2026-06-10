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

set(IIR1_SRC
	iir1/Iir.h
	iir1/iir/Biquad.cpp
	iir1/iir/Butterworth.cpp
	iir1/iir/Cascade.cpp
	iir1/iir/ChebyshevI.cpp
	iir1/iir/ChebyshevII.cpp
	iir1/iir/Custom.cpp
	iir1/iir/PoleFilter.cpp
	iir1/iir/RBJ.cpp
)

gmpi_plugin(
	PROJECT_NAME Butterworth
	HAS_DSP
	SOURCE_FILES
	  Butterworth.cpp
	  ButterworthIir.h
	  ${IIR1_SRC}
	  ${GMPI_SDK}/Helpers/FilterBase.h
)

source_group(iir1 FILES ${IIR1_SRC})

# Don't build the filter-design maths with "fast math" (mirrors modules/Filters:
# fast-math breaks the Butterworth high-pass passband normalisation in Release builds).
target_compile_options(Butterworth PRIVATE
	$<$<CXX_COMPILER_ID:MSVC>:/fp:precise>
	$<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fno-fast-math>
)

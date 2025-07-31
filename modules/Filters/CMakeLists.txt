cmake_minimum_required(VERSION 3.15)

project(Filters)

set( DSP_FILTERS_SRC
        DspFilters/Biquad.h
        DspFilters/Biquad.cpp
        DspFilters/Butterworth.h
        DspFilters/Butterworth.cpp
        DspFilters/Cascade.h
        DspFilters/Cascade.cpp
        DspFilters/Elliptic.h
        DspFilters/Elliptic.cpp
        DspFilters/PoleFilter.h
        DspFilters/PoleFilter.cpp
        DspFilters/Common.h
        DspFilters/Layout.h
        DspFilters/MathSupplement.h
)

build_gmpi_plugin(
    PROJECT_NAME ${PROJECT_NAME}
    IS_OFFICIAL_MODULE
    HAS_DSP
    SOURCE_FILES
        IIR_Filters2.cpp
        Lookahead.cpp
        ButterworthLp.cpp
        ButterworthLp.h
        ../shared/FilterBase.h
        ${DSP_FILTERS_SRC}

)

source_group(DspFilter FILES ${DSP_FILTERS_SRC})

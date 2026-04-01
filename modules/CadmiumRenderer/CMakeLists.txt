cmake_minimum_required(VERSION 3.30)

project(CadmiumRenderer)

# New GMPI UI SDK sources
set(sdk_srcs
    ${GMPI_SDK}/Core/Common.h
    ${GMPI_SDK}/Core/Common.cpp
    ${GMPI_SDK}/Core/RefCountMacros.h
    ${GMPI_SDK}/Core/GmpiApiCommon.h
    ${GMPI_SDK}/Core/GmpiSdkCommon.h
    ${GMPI_SDK}/Core/GmpiApiEditor.h
    ${GMPI_UI_SDK}/GmpiApiDrawing.h
    ${GMPI_UI_SDK}/Drawing.h
    ${GMPI_UI_SDK}/helpers/Timer.h
    ${GMPI_UI_SDK}/helpers/Timer.cpp
)

set(srcs
    CadmiumRenderer.cpp
    RenderGui.cpp
    SolidColorBrushGui.cpp
    SquareGui.cpp
    Cadmium/GUI_3_0.cpp
    Cadmium/GUI_3_0.h
    jsoncpp/jsoncpp.cpp
    jsoncpp/json/json.h
)

source_group(sdk FILES ${sdk_srcs})

add_library(${PROJECT_NAME} MODULE ${srcs} ${sdk_srcs})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${GMPI_SDK}
    ${GMPI_SDK}/Core
    ${GMPI_UI_SDK}
    ${CMAKE_CURRENT_SOURCE_DIR}/../se_sdk3_hosting
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE d3d11 d2d1 dwrite windowscodecs)
    target_link_options(${PROJECT_NAME} PRIVATE "/SUBSYSTEM:WINDOWS")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES SUFFIX ".sem")
set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "modules")

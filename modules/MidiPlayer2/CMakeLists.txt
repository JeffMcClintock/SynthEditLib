cmake_minimum_required(VERSION 3.15)

project(MidiPlayer2)

build_gmpi_plugin(
    PROJECT_NAME ${PROJECT_NAME}
    IS_OFFICIAL_MODULE
    HAS_DSP
    SOURCE_FILES 
        MidiPlayer1.h
        MidiPlayer2.h
        MidiPlayer1.cpp
        MidiPlayer2.cpp
        MidiRecorder.cpp
        MidiToGate.cpp
        MidiMessage.cpp
        smf.h
        smf.cpp
)

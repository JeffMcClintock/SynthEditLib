cmake_minimum_required(VERSION 3.30)

# To build SE Modules, point CMake at 'SE16' folder, not here.
# for ARM64, use the following commands:
#  cd build_arm (C:SE/SynthEditLib/build_arm)
#  cmake ../../SE16 -G "Visual Studio 17 2022" -A ARM64   (or ARM64EC, fails with SIMDUTF8 lib)

# macOS 13.3 required for std::format (SubControls4)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment version")
set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build architectures for macOS" FORCE)
set(SE_GRAPHICS_SUPPORT TRUE CACHE BOOL "Support Graphics modules")
set(CMAKE_CXX_STANDARD 20)

# CPM bootstrap (shared cache across builds)
set(CPM_DOWNLOAD_VERSION 0.38.6)
set(CPM_USE_NAMED_CACHE_DIRECTORIES ON)
set(CPM_GIT_SHALLOW ON) # shallow clone for speed

if(NOT DEFINED CPM_SOURCE_CACHE)
  if(WIN32)
    set(CPM_SOURCE_CACHE "$ENV{USERPROFILE}/.cpm" CACHE STRING "CPM cache dir")
  else()
    set(CPM_SOURCE_CACHE "$ENV{HOME}/.cache/CPM" CACHE STRING "CPM cache dir")
  endif()
endif()

set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
if(NOT EXISTS "${CPM_DOWNLOAD_LOCATION}")
  file(DOWNLOAD
    "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
    "${CPM_DOWNLOAD_LOCATION}"
    TLS_VERIFY ON
  )
endif()
include("${CPM_DOWNLOAD_LOCATION}")

set(DSP_CORE ${CMAKE_CURRENT_SOURCE_DIR})

project(SynthEditLib)

set(sdk_srcs
${GMPI_SDK}/Core/GmpiApiCommon.h
${GMPI_SDK}/Core/Processor.h
${GMPI_SDK}/Core/Processor.cpp

${GMPI_UI_SDK}/GmpiApiDrawing.h
${GMPI_UI_SDK}/Drawing.h
${GMPI_UI_SDK}/helpers/GmpiPluginEditor.h
${GMPI_UI_SDK}/helpers/Timer.h
${GMPI_UI_SDK}/helpers/Timer.cpp
${GMPI_UI_SDK}/helpers/NativeUi.h
${GMPI_UI_SDK}/helpers/ImageCache.h
${GMPI_UI_SDK}/helpers/ImageCache.cpp
${GMPI_UI_SDK}/helpers/ImageMetadata.h
${GMPI_UI_SDK}/helpers/ImageMetadata.cpp
${GMPI_UI_SDK}/helpers/AnimatedBitmap.h
${GMPI_UI_SDK}/backends/DrawingFrameCommon.h
${GMPI_UI_SDK}/backends/DrawingFrameCommon.cpp

${se_sdk_folder}/mp_sdk_common.h
${se_sdk_folder}/mp_sdk_common.cpp
${se_sdk_folder}/mp_sdk_audio.h
${se_sdk_folder}/mp_sdk_audio.cpp
${se_sdk_folder}/mp_sdk_gui.h
${se_sdk_folder}/mp_sdk_gui.cpp
${se_sdk_folder}/TimerManager.h
${se_sdk_folder}/TimerManager.cpp
${se_sdk_folder}/smart_audio_pin.h
${se_sdk_folder}/smart_audio_pin.cpp
${se_sdk_folder}/Drawing.h
${se_sdk_folder}/ProtectedFile.h

${se_shared_folder}/it_enum_list.h
${se_shared_folder}/it_enum_list.cpp
${se_shared_folder}/FileFinder.h
${se_shared_folder}/FileFinder.cpp
${se_shared_folder}/FileWatcher.h
${se_shared_folder}/FileWatcher.cpp
${se_shared_folder}/PatchCables.h
${se_shared_folder}/PatchCables.cpp
${se_shared_folder}/simdutf/simdutf.h
${se_shared_folder}/simdutf/simdutf.cpp
${se_shared_folder}/wav_file.h
${se_shared_folder}/wav_file.cpp
${se_shared_folder}/xp_dynamic_linking.h
${se_shared_folder}/xp_dynamic_linking.cpp
${se_shared_folder}/xplatform_modifier_keys.h
${se_shared_folder}/xplatform_modifier_keys.cpp
${GMPI_SDK}/Hosting/message_queues.h
${GMPI_SDK}/Hosting/message_queues.cpp
)

IF(WIN32)
set(sdk_srcs
  ${sdk_srcs}
  ${GMPI_UI_SDK}/backends/DrawingFrameWin.h
  ${GMPI_UI_SDK}/backends/DrawingFrameWin.cpp
  ${GMPI_UI_SDK}/backends/DirectXGfx.h
  ${GMPI_UI_SDK}/backends/DirectXGfx.cpp
  Shared/DrawingFrame2_win.h
  Shared/DrawingFrame2_win.cpp
)
endif()

set(srcs_json
Shared/jsoncpp/json/json.h
Shared/jsoncpp/jsoncpp.cpp
)

set(srcs_xml
tinyxml/tinyxml.h
tinyxml/tinyxml.cpp
tinyxml/tinyxmlerror.cpp
tinyxml/tinyxmlparser.cpp
modules/tinyXml2/tinyxml2.h
modules/tinyXml2/tinyxml2.cpp
)

set(srcs_modules_internal
modules_internal/Blob2Test.cpp
modules_internal/CadmiumModules.cpp
modules_internal/CpuMeter/CpuMeterGui.cpp
modules_internal/MIDI2Converter.cpp
modules_internal/MPEToMIDI2.cpp
modules_internal/PatchPoints.cpp
modules_internal/PatchPointsGui.h
modules_internal/PatchPointsGui.cpp
modules_internal/PrefabBrowser/PrefabBrowserGui.cpp
modules_internal/RegistrationCheck.cpp
modules_internal/VoiceMute/VoiceMute.h
modules_internal/VoiceMute/VoiceMute.cpp
)

if(SE_GRAPHICS_SUPPORT)
set(srcs_modules_internal
${srcs_modules_internal}
modules/SubControls4/GmpiUiTest.cpp
modules/SubControls4/half.hpp
modules/SubControls4/NumberEditClient.h
)
endif()

set(srcs_dspfilters
DspFilters/Biquad.cpp
DspFilters/Butterworth.cpp
DspFilters/Cascade.cpp
DspFilters/Elliptic.cpp
DspFilters/Filter.cpp
DspFilters/Param.cpp
DspFilters/PoleFilter.cpp
DspFilters/Biquad.h
DspFilters/Cascade.h
DspFilters/Common.h
DspFilters/Design.h
DspFilters/Elliptic.h
DspFilters/Filter.h
DspFilters/Layout.h
DspFilters/MathSupplement.h
DspFilters/Params.h
DspFilters/PoleFilter.h
DspFilters/State.h
DspFilters/Types.h
)

set(srcs
Shared/AuPreset.h
Shared/AuPreset.cpp

modules/se_sdk3_hosting/IGuiHost2.h

${se_sdk_hosting_folder}/gmpi_gui_hosting.h
${se_sdk_hosting_folder}/gmpi_gui_hosting.cpp
${se_sdk_hosting_folder}/DirectXGfx.h
${se_sdk_hosting_folder}/DirectXGfx.cpp
${se_sdk_hosting_folder}/Controller.h
${se_sdk_hosting_folder}/Controller.cpp
${se_sdk_hosting_folder}/ViewBase.h
${se_sdk_hosting_folder}/ViewBase.cpp
${se_sdk_hosting_folder}/ContainerView.h
${se_sdk_hosting_folder}/ContainerView.cpp
${se_sdk_hosting_folder}/SubViewPanel.h
${se_sdk_hosting_folder}/SubViewPanel.cpp
${se_sdk_hosting_folder}/MpParameter.h
${se_sdk_hosting_folder}/MpParameter.cpp
${se_sdk_hosting_folder}/ConnectorView.h
${se_sdk_hosting_folder}/ConnectorView.cpp
${se_sdk_hosting_folder}/ModuleView.h
${se_sdk_hosting_folder}/ModuleView.cpp
${se_sdk_hosting_folder}/GmpiResourceManager.h
${se_sdk_hosting_folder}/GmpiResourceManager.cpp
${se_sdk_hosting_folder}/GuiPatchAutomator3.h
${se_sdk_hosting_folder}/GuiPatchAutomator3.cpp
${se_sdk_hosting_folder}/IViewChild.h
${se_sdk_hosting_folder}/IViewChild.cpp
${se_sdk_hosting_folder}/JsonDocPresenter.h
${se_sdk_hosting_folder}/BundleInfo.cpp
${se_sdk_hosting_folder}/BundleInfo.h
${se_sdk_hosting_folder}/IGuiHost2.h
${se_sdk_hosting_folder}/ElatencyContraintType.h
${se_sdk_hosting_folder}/PresetReader.h

Shared/VstPreset.h
Shared/VstPreset.cpp
Shared/Vst2Preset.h
Shared/Vst2Preset.cpp
Shared/se_logger.h
Shared/se_logger.cpp

${VST3_SDK}/pluginterfaces/base/funknown.h
${VST3_SDK}/pluginterfaces/base/funknown.cpp
${VST3_SDK}/pluginterfaces/base/coreiids.cpp
${VST3_SDK}/pluginterfaces/base/smartpointer.h
${VST3_SDK}/pluginterfaces/base/fplatform.h
${VST3_SDK}/pluginterfaces/base/ftypes.h
${VST3_SDK}/pluginterfaces/base/iupdatehandler.h
${VST3_SDK}/pluginterfaces/base/ibstream.h
${VST3_SDK}/pluginterfaces/base/icloneable.h
${VST3_SDK}/pluginterfaces/base/ipluginbase.h
${VST3_SDK}/pluginterfaces/base/fstrdefs.h
${VST3_SDK}/pluginterfaces/base/iplugincompatibility.h
${VST3_SDK}/public.sdk/source/vst/vstpresetfile.h
${VST3_SDK}/public.sdk/source/vst/vstpresetfile.cpp
${VST3_SDK}/public.sdk/source/vst/vstinitiids.cpp
${VST3_SDK}/base/source/fobject.h
${VST3_SDK}/base/source/fobject.cpp
${VST3_SDK}/base/source/fbuffer.h
${VST3_SDK}/base/source/fbuffer.cpp
${VST3_SDK}/base/source/fstring.h
${VST3_SDK}/base/source/fstring.cpp
${VST3_SDK}/base/source/fdebug.h
${VST3_SDK}/base/source/fdebug.cpp
${VST3_SDK}/base/source/baseiids.cpp
${VST3_SDK}/base/source/updatehandler.h
${VST3_SDK}/base/source/updatehandler.cpp
${VST3_SDK}/base/source/classfactoryhelpers.h
${VST3_SDK}/base/thread/include/flock.h
${VST3_SDK}/base/thread/source/flock.cpp

UgDebugInfo.cpp

Base64.h
cancellation.h
CMakeLists.txt
conversion.cpp
conversion.h
CpuConsumption.h
CVoiceList.cpp
CVoiceList.h
datatype_to_id.cpp
datatype_to_id.h
denormal_fixer.h
DspFilterJeff.h
DspPatchManagerProxy.cpp
DspPatchManagerProxy.h
dsp_msg_target.h
dsp_patch_manager.cpp
dsp_patch_manager.h
dsp_patch_parameter.cpp
dsp_patch_parameter.h
dsp_patch_parameter_base.h
EventProcessor.cpp
EventProcessor.h
fixed_memory_manager.h
HostControls.cpp
HostControls.h
HostVoiceControl.h
IDspPatchManager.h
InterfaceObject.cpp
InterfaceObject.h
IPluginGui.cpp
IPluginGui.h
ISeAudioMaster.h
iseshelldsp.h
iterator.h
Logic.h
MidiEvent.h
MidiToCv2.cpp
MidiToCv2.h
midi_defs.cpp
midi_defs.h
module_info.h
Module_Info3.cpp
Module_Info3.h
Module_Info3_base.cpp
Module_Info3_base.h
Module_Info3_internal.h
module_register.h
MpPinIterator.h
MySmartPtr.h
MyTypeTraits.cpp
MyTypeTraits.h
notify.cpp
notify.h
Notify_msg.h
PatchStorage.cpp
PatchStorage.h
pch.cpp
pch.h
plug_description.h
RawConversions.cpp
RawConversions.h
resource.h
sample.h
Sdk3CloneIterator.h
SeAudioMaster.cpp
SeAudioMaster.h
HoverScopeAudioCollector.cpp
HoverScopeAudioCollector.h
SerializationHelper_XML.h
se_types.h
SincFilter.h
SincFilter.cpp
smart_output.h
srange.h
SynthRuntime.cpp
SynthRuntime.h
TypeToEDatatype.h
UgDatabase.cpp
UgDatabase.h
ug_adder2.cpp
ug_adder2.h
ug_adsr.cpp
ug_adsr.h
ug_base.cpp
ug_base.h
ug_clipper.cpp
ug_clipper.h
ug_combobox.cpp
ug_combobox.h
ug_comparator.cpp
ug_comparator.h
ug_container.cpp
ug_container.h
ug_control.h
ug_cross_fade.cpp
ug_cross_fade.h
ug_cv_midi.cpp
ug_cv_midi.h
ug_default_setter.cpp
ug_default_setter.h
ug_delay.cpp
ug_delay.h
ug_denormal_detect.cpp
ug_denormal_detect.h
ug_denormal_stop.cpp
ug_denormal_stop.h
ug_envelope.cpp
ug_envelope.h
ug_envelope_base.cpp
ug_envelope_base.h
ug_event.h
ug_feedback_delays.cpp
ug_feedback_delays.h
ug_filter_1pole_lp.cpp
ug_filter_1pole_lp.h
ug_filter_allpass.cpp
ug_filter_allpass.h
ug_filter_biquad.cpp
ug_filter_biquad.h
ug_filter_sv.cpp
ug_filter_sv.h
ug_fixed_values.cpp
ug_fixed_values.h
ug_flags.h
ug_float_to_volts.cpp
ug_float_to_volts.h
ug_generic_1_1.cpp
ug_generic_1_1.h
ug_gmpi.cpp
ug_gmpi.h
ug_io_mod.cpp
ug_io_mod.h
ug_latency_adjust.h
ug_latency_adjust_event.h
ug_logic_Bin_Count.cpp
ug_logic_Bin_Count.h
ug_logic_complex.cpp
ug_logic_complex.h
ug_logic_counter.cpp
ug_logic_counter.h
ug_logic_decade.cpp
ug_logic_decade.h
ug_logic_gate.cpp
ug_logic_gate.h
ug_logic_not.cpp
ug_logic_not.h
ug_logic_shift.cpp
ug_logic_shift.h
ug_math_base.cpp
ug_math_base.h
ug_math_ceil.cpp
ug_math_ceil.h
ug_math_floor.cpp
ug_math_floor.h
ug_midi_automator.cpp
ug_midi_automator.h
ug_midi_controllers.cpp
ug_midi_controllers.h
ug_midi_device.cpp
ug_midi_device.h
ug_midi_filter.cpp
ug_midi_filter.h
ug_midi_keyboard.cpp
ug_midi_keyboard.h
ug_midi_to_cv.cpp
ug_midi_to_cv.h
ug_monostable.cpp
ug_monostable.h
ug_multiplier.cpp
ug_multiplier.h
ug_notesource.h
ug_oscillator2.cpp
ug_oscillator2.h
ug_oscillator_pd.cpp
ug_oscillator_pd.h
ug_oversampler.cpp
ug_oversampler.h
ug_oversampler_in.h
ug_oversampler_io.h
ug_oversampler_out.cpp
ug_oversampler_out.h
ug_pan.cpp
ug_pan.h
ug_patch_automator.cpp
ug_patch_automator.h
ug_patch_param_setter.cpp
ug_patch_param_setter.h
ug_patch_param_watcher.cpp
ug_patch_param_watcher.h
ug_peak_det.cpp
ug_peak_det.h
ug_plugin3.cpp
ug_plugin3.h
ug_quantiser.cpp
ug_quantiser.h
ug_random.cpp
ug_random.h
ug_sample_hold.cpp
ug_sample_hold.h
ug_slider.cpp
ug_slider.h
ug_switch.cpp
ug_switch.h
ug_switch2.h
ug_system_command.cpp
ug_system_command.h
ug_system_modules.cpp
ug_test_tone.cpp
ug_test_tone.h
ug_text_entry.cpp
ug_text_entry.h
ug_vca.cpp
ug_vca.h
ug_voice_monitor.cpp
ug_voice_monitor.h
ug_voice_splitter.cpp
ug_voice_splitter.h
ug_voltage_to_enum.cpp
ug_voltage_to_enum.h
ug_volts_to_float.cpp
ug_volts_to_float.h
ug_vst_in.cpp
ug_vst_in.h
ug_vst_out.cpp
ug_vst_out.h
ug_wave_player.cpp
ug_wave_player.h
ug_wave_recorder.cpp
ug_wave_recorder.h
ULookup.cpp
ULookup.h
UMidiBuffer2.h
UniqueSnowflake.cpp
UniqueSnowflake.h
UPlug.cpp
UPlug.h
USampBlock.h
variable_policies.cpp
variable_policies.h
se_file_format_version.h
conversion.h
conversion.cpp
)

IF(APPLE)
set(srcs ${srcs}
modules/se_sdk3_hosting/Cocoa_Gfx.h
modules/se_sdk3_hosting/EventHelper.h
modules/se_sdk3_hosting/SynthEditCocoaView.mm
modules/se_sdk3_hosting/CocoaGuiHost.h
modules/se_sdk3_hosting/CocoaGuiHost.mm
modules/se_sdk3_hosting/gmpi_drawing_conversions.h
${GMPI_UI_SDK}/backends/DrawingFrameMac.mm
)
ENDIF()


IF(SE2JUCE)
set(srcs
${srcs}
modules/shared/real_fft.h
modules/shared/real_fft.cpp
modules/shared/expression_evaluate.h
modules/shared/expression_evaluate.cpp

modules/EnvelopeAdsr/Adsr.cpp
modules/EnvelopeAdsr/envelope.cpp
modules/Converters/FloatScaler2Gui.cpp
modules/Converters/FloatToText.cpp
modules/Converters/FloatToVolts2.cpp
modules/ControlsXp/FreqAnalyser.cpp
modules/ControlsXp/FreqAnalyser3.cpp
modules/ControlsXp/ListEntry.cpp
modules/ControlsXp/SpectrumAnalyserBase.h
modules/ControlsXp/SpectrumAnalyserBase.cpp
modules/ControlsXp/Scope3.cpp
modules/Converters/VoltsToMidiCc.cpp

modules/PatchMemory/PatchInfoGui.h
modules/PatchMemory/PatchMemoryBoolGui.h
modules/PatchMemory/PatchMemoryBoolOutGui.h
modules/PatchMemory/PatchMemoryFloatGui.h
modules/PatchMemory/PatchMemoryFloatOutGui.h
modules/PatchMemory/PatchMemoryList.h
modules/PatchMemory/PatchMemoryListGui.h
modules/PatchMemory/PatchMemoryTextOutGui.h

modules/PatchMemory/PatchMemoryBool.cpp
modules/PatchMemory/PatchMemoryFloat.cpp
modules/PatchMemory/PatchMemoryInt.cpp
modules/PatchMemory/PatchMemoryList.cpp
modules/PatchMemory/PatchMemoryText.cpp
modules/PatchMemory/PatchBrowserGui.cpp
modules/PatchMemory/PatchInfoGui.cpp
modules/PatchMemory/PatchMemoryBoolGui.cpp
modules/PatchMemory/PatchMemoryBoolOutGui.cpp
modules/PatchMemory/PatchMemoryfloatGui.cpp
modules/PatchMemory/PatchMemoryFloatOutGui.cpp
modules/PatchMemory/PatchMemoryListGui.cpp
modules/PatchMemory/PatchMemoryTextOutGui.cpp

modules/FloatLimiter/FloatLimiterGui.cpp
modules/SubControlsXp/Image2GuiXp.cpp
modules/SubControlsXp/PanelGroupGui.cpp
modules/SubControlsXp/PlainImageGui.cpp
modules/PolyphonyControl/PolyphonyControlGui.cpp
modules/SubControlsXp/PopupMenuGui.cpp
modules/SubControlsXp/TextEntry4Gui.cpp
modules/SubControlsXp/TextSubcontrol.cpp
modules/UserSetting/UserSettingText.cpp
modules/UserSetting/UserSettingTextController.cpp
modules/UserSetting/UserSettingTextGui.cpp
modules/VaFilters/SvFilter.cpp
modules/BPMClock3/BpmClock3.cpp
modules/BPMClock3/BpmTempo.cpp
modules/Filters/ButterworthLp.cpp
modules/Converters/Converters.cpp
modules/Converters/BlobToBlob2.cpp
modules/ImpulseResponse/ImpulseResponse.cpp
modules/Increment3/Increment3Gui.cpp
modules/Inverter/Inverter.cpp
modules/MidiPlayer2/MidiToGate.cpp
"modules/MIDI Logger/IdeLogger.cpp"
modules/Converters/my_type_convert.cpp
modules/NoteExpression/NoteExpression.cpp
modules/OscillatorNaive/OscillatorNaive.cpp
modules/OversamplingControl/OversamplingControlGui.cpp
modules/ControlsXp/Slider.cpp
modules/SoftDistortion/SoftDistortion.cpp
modules/Switches/Switches.cpp
modules/Switches/SilenceGate.cpp
modules/Converters/UnitConverter.cpp
modules/ControlsXp/VoltMeter.cpp
modules/ControlsXp/Waveshaper3Xp.cpp
modules/Waveshapers/Waveshapers.cpp
modules/EnvelopeAdsr/Adsr.h
modules/EnvelopeAdsr/Envelope.h
modules/ControlsXp/ClassicControlGuiBase.h
modules/ControlsXp/FreqAnalyserGui.h
modules/ControlsXp/ListEntry.h
modules/ControlsXp/Scope3.h
modules/ControlsXp/Scope3Gui.h
modules/ControlsXp/SliderGui.h
modules/ControlsXp/VoltMeterGui.h
modules/BoolInverter/BoolInverterGui.h
modules/Converters/Converters.h
modules/Converters/ConvertersGui.h
modules/Converters/FloatToText.h
modules/Converters/VoltsToMidiCc.h

modules/sharedLegacyWidgets/BitmapWidget.h
modules/FloatLimiter/FloatLimiterGui.h
modules/SubControlsXp/Image2GuiXp.h
modules/SubControlsXp/TextSubcontrol.h
modules/UserSetting/SettingFile.h
modules/VaFilters/SvFilter.h
modules/Filters/ButterworthLp.h
modules/ControlsXp/FreqAnalyser.h
modules/Increment3/Increment3Gui.h
modules/OscillatorNaive/OscillatorNaive.h
modules/OversamplingControl/OversamplingControlGui.h
modules/ControlsXp/Slider.h
modules/ControlsXp/VoltMeter.h

# Common GUI modules
modules/sharedLegacyWidgets/EditWidget.h
modules/sharedLegacyWidgets/ImageBase.h
modules/sharedLegacyWidgets/ListWidget.h
modules/sharedLegacyWidgets/RotarySwitchWidget.h
modules/sharedLegacyWidgets/SubControlBase.h
modules/sharedLegacyWidgets/TextEditWidget.h
modules/sharedLegacyWidgets/TextWidget.h
modules/sharedLegacyWidgets/Widget.h
modules/sharedLegacyWidgets/WidgetHost.h

modules/sharedLegacyWidgets/BitmapWidget.cpp
modules/sharedLegacyWidgets/EditWidget.cpp
modules/sharedLegacyWidgets/ImageBase.cpp
modules/sharedLegacyWidgets/ListWidget.cpp
modules/sharedLegacyWidgets/TextEditWidget.cpp
modules/sharedLegacyWidgets/TextWidget.cpp
modules/sharedLegacyWidgets/Widget.cpp

modules/shared/ImageCache.h
modules/shared/ImageCache.cpp
modules/shared/FontCache.h
modules/shared/FontCache.cpp
modules/shared/ImageMetadata.h
modules/shared/ImageMetadata.cpp

modules/ControlsXp/ListEntryGui.h
modules/ControlsXp/ListEntryGui.cpp
modules/ControlsXp/FreqAnalyserGui.cpp
modules/ControlsXp/ClassicControlGuiBase.cpp
modules/ControlsXp/Scope3Gui.cpp
modules/ControlsXp/SliderGui.h
modules/ControlsXp/SliderGui.cpp
modules/ControlsXp/VoltMeterGui.cpp

modules/SubControlsXp/PanelGroupGui.h
modules/SubControlsXp/PopupMenuGui.h
modules/SubControlsXp/ListEntry4Gui.cpp
modules/SubControlsXp/ListEntry4Gui.h
modules/SubControlsXp/TextEntry4Gui.h
modules/SubControlsXp/PlainImageGui.h
modules/SubControlsXp/PlainImageGui.cpp

modules/BoolInverter/BoolInverterGui.cpp
modules/Converters/ConvertersGui.cpp
modules/PolyphonyControl/PolyphonyControlGui.h

modules/ControlsXp/ImpulseResponse2.cpp      
modules/ImpulseResponse/Impulse.cpp      
modules/Filters/IIR_Filters2.cpp      

)
endif()

# message(STATUS "DSP_CORE: ${DSP_CORE}")
# message(STATUS "se_sdk_folder: ${se_sdk_folder}")

include_directories(
${DSP_CORE}
${DSP_CORE}/modules
${se_sdk_folder}
${VST3_SDK}
${GMPI_SDK}
${GMPI_SDK}/Core
${GMPI_UI_SDK}
${se_shared_folder}
${se_sdk_hosting_folder}
)

source_group(se_sdk FILES ${sdk_srcs})
source_group(json FILES ${srcs_json})
source_group(modules_internal FILES ${srcs_modules_internal})
source_group(xml FILES ${srcs_xml})
source_group(DspFilters FILES ${srcs_dspfilters})

if(APPLE)
FIND_LIBRARY(COREFOUNDATION_LIBRARY CoreFoundation )
MARK_AS_ADVANCED (COREFOUNDATION_LIBRARY)
endif()

include (GenerateExportHeader)
add_library(${PROJECT_NAME} STATIC ${sdk_srcs} ${srcs} ${srcs_json} ${srcs_modules_internal} ${srcs_xml} ${srcs_dspfilters})

target_compile_definitions(${PROJECT_NAME} PRIVATE 
  $<$<CONFIG:Debug>:_DEBUG>
  $<$<CONFIG:Release>:NDEBUG>
)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)

if(APPLE)

  # generate debug symbols
  target_compile_options(${PROJECT_NAME} PRIVATE
      $<$<CONFIG:Debug>:-g>
      -Wno-deprecated-declarations
  )
  set_target_properties(${PROJECT_NAME} PROPERTIES XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT
    $<$<CONFIG:Debug>:dwarf-with-dsym>
  )

TARGET_LINK_LIBRARIES( ${PROJECT_NAME} ${COREFOUNDATION_LIBRARY} )
endif()

IF(WIN32)
target_link_libraries(${PROJECT_NAME} Dwmapi dxguid d3d11.lib d2d1 dwrite windowscodecs)
endif()

target_compile_definitions (${PROJECT_NAME} PRIVATE
    _UNICODE
    UNICODE
    SE_USE_LIB_FACTORY=1
    COMPILE_HOST_SUPPORT
)

# enable super fast UTF conversions
target_compile_definitions(
  ${PROJECT_NAME} PUBLIC 
  USE_SIMD_UTF_CONVERSION
  SE_GRAPHICS_SUPPORT=${SE_GRAPHICS_SUPPORT}
)

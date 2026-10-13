#pragma once

#include <string>
#include "mp_api.h"
#include "Core/GmpiApiEditor.h"
#include "RawView.h"

namespace SE2
{
	class IPresenter;
}

class IGuiHost2
{
public:
	virtual int32_t RegisterGui2(gmpi::api::IParameterObserver* /*gui*/) { return 0; }
	virtual int32_t UnRegisterGui2(gmpi::api::IParameterObserver* /*gui*/) { return 0; }
	virtual RawView getParameterValue(int32_t parameterHandle, int32_t fieldId = gmpi::MP_FT_VALUE, int32_t voice = 0) = 0;
	virtual void setParameterValue(RawView /*value*/, int32_t /*parameterHandle*/, gmpi::FieldType /*moduleFieldId*/ = gmpi::MP_FT_VALUE, int32_t /*voice*/ = 0) {}
	virtual void initializeGui(gmpi::api::IParameterObserver* gui, int32_t parameterHandle, gmpi::Field FieldId) = 0;
	virtual int32_t sendSdkMessageToAudio(int32_t handle, int32_t id, int32_t size, const void* messageData) = 0;
	virtual int32_t getParameterHandle(int32_t moduleHandle, int32_t moduleParameterId) = 0;
	virtual int32_t getParameterModuleAndParamId(int32_t parameterHandle, int32_t* returnModuleHandle, int32_t* returnModuleParameterId) = 0;
	virtual int32_t resolveFilename(const wchar_t* shortFilename, int32_t maxChars, wchar_t* returnFullFilename) = 0;
	virtual int32_t getController(int32_t handle, gmpi::IMpController** returnController) = 0;
	virtual void serviceGuiQueue() = 0;
	virtual void LoadNativePresetFile(std::string /*presetName*/) {}
	virtual void setMainPresenter(SE2::IPresenter* presenter) = 0;
	virtual void undoTransanctionStart() = 0;
	virtual void undoTransanctionEnd() = 0;
};

// Hacky back-channel (same idiom as Processor_VST3.cpp's "dataptr" for MIDI-to-host): the GMPI
// plugin factory's createInstance(id, PluginSubtype::Editor) takes no context parameter, so a
// VST3 Controller_VST3 (which IS an IGuiHost2 via MpController) has no other way to hand itself
// to the SeGmpiEditor it's about to construct one call later. Set immediately before that
// createInstance call, read (and left alone - NOT cleared, next creation overwrites it) by the
// editor's constructor. Single-threaded and non-reentrant: one editor is under construction at
// a time. Null when there's no such controller to hand over (e.g. the Standalone wrapper), in
// which case the reader falls back to a self-contained host.
extern IGuiHost2* g_pendingEditorGuiHost;
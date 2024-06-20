
#pragma once
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#include <string>
#include <vector>
#include <unordered_map>
#include "modules/shared/xplatform.h"
#include "modules/shared/RawView.h"
#include "ElatencyContraintType.h"
#include "se_types.h"
#include "mp_midi.h"

/*
#include "iseshelldsp.h"
*/

enum class audioMasterState { Starting, Running, AsyncRestart, Stopping, Stopped };

class ISpecialIoModule
{
public:
	virtual ~ISpecialIoModule() {}
};

class ISpecialIoModuleAudio : public ISpecialIoModule
{
public:
};

class ISpecialIoModuleAudioIn : public ISpecialIoModuleAudio
{
public:
	virtual void setIoBuffers(const float* const* p_outputs, int numChannels) = 0;
	virtual void sendMidi(timestamp_t clock, const gmpi::midi::message_view& msg) = 0;
	virtual void setInputSilent(int input, bool isSilent) = 0;
	virtual int getAudioInputCount() = 0;
	virtual bool wantsMidi() = 0;
	virtual void setMpeMode(int32_t mpemode) = 0;
};

class ISpecialIoModuleAudioOut : public ISpecialIoModuleAudio
{
public:
	virtual void setIoBuffers(float* const* p_outputs, int numChannels) = 0;
	virtual int getAudioOutputCount() = 0;
	virtual bool sendsMidi() = 0;
	virtual class MidiBuffer3* getMidiOutputBuffer() = 0;
	virtual void startFade(bool isDucked) = 0;
	virtual uint64_t getSilenceFlags(int output, int count) = 0;
	virtual int getOverallPluginLatencySamples() = 0;
};

// Core Functionality provided by all environments.
// Needs clarification as to threading.
class ISeShellDsp
{
public:
	virtual class IWriteableQue* MessageQueToGui() = 0;
	virtual void ServiceDspRingBuffers() = 0;
	virtual void ServiceDspWaiters2(int sampleframes, int guiFrameRateSamples) = 0;
	virtual void RequestQue( class QueClient* client, bool noWait = false ) = 0;
	virtual void NeedTempo() = 0;

	virtual std::wstring ResolveFilename(const std::wstring& name, const std::wstring& extension) = 0;
	virtual std::wstring getDefaultPath(const std::wstring& p_file_extension ) = 0;
	virtual void GetRegistrationInfo(std::wstring& p_user_email, std::wstring& p_serial) = 0;
	virtual void DoAsyncRestart() = 0;

    virtual void OnSaveStateDspStalled() = 0;
    
    // Ducking fade-out complete.
    virtual void OnFadeOutComplete() = 0;

	virtual int32_t SeMessageBox(const wchar_t* msg, const wchar_t* title, int flags) = 0;

	virtual std::unordered_map<int32_t, int32_t>& GetModuleLatencies() = 0;

	virtual int RegisterIoModule(ISpecialIoModule*) = 0;
	virtual void onSetParameter(int32_t handle, RawView rawValue, int voiceId) = 0;
	virtual void EnableIgnoreProgramChange() = 0;
	virtual bool isEditor() = 0;
	virtual void SetCancellationMode() = 0;
};

// implement common functionality
class SeShellDsp : public ISeShellDsp
{
public:
	std::unordered_map<int32_t, int32_t>& GetModuleLatencies() override
	{
		return moduleLatencies;
	}

	virtual void Clear()
	{
		moduleLatencies.clear();
	}

private:
	std::unordered_map<int32_t, int32_t> moduleLatencies;
};

#pragma once
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include "modules/shared/xplatform.h"
#include "modules/shared/RawView.h"
#include "se_types.h"
#include "mp_midi.h"

/*
#include "iseshelldsp.h"
*/

enum class audioMasterState { Starting, Running, AsyncRestart, Stopping, Stopped };

namespace gmpi
{
namespace hosting
{
class QueClient;
class IWriteableQue;
}
}

class ISpecialIoModule
{
public:
	virtual ~ISpecialIoModule() {}
};

class ISpecialIoModuleAudio : public ISpecialIoModule
{
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
	virtual gmpi::hosting::IWriteableQue* MessageQueToGui() = 0;
	virtual void ServiceDspRingBuffers() = 0;
	virtual void ServiceDspWaiters2(int sampleframes) = 0;
	virtual void RequestQue(gmpi::hosting::QueClient* client, bool noWait = false ) = 0;
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
	virtual std::unordered_map<int64_t, std::string>* getExtraPinDefaultChanges() {return {};} // Editor only

	virtual int RegisterIoModule(ISpecialIoModule*) = 0;
	virtual void onSetParameter(int32_t handle, int32_t field, RawView rawValue, int voiceId) = 0;
	virtual void EnableIgnoreProgramChange() = 0;
	virtual bool isEditor() = 0;
	virtual void SetCancellationMode() = 0;
};

enum class eRuntimeState { idling, newDspReady, newDspFailed, running, resetting, stopped };

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
	
	// Ducking fade-out complete.
	void OnFadeOutComplete() override
	{
		// we're in the call stack of seaudiomaster, so just flag need for rebuild.
//		fadeoutdone = true;
		runtimeState = eRuntimeState::resetting;
	}

protected:
	std::atomic<eRuntimeState> runtimeState = eRuntimeState::idling;
	std::unique_ptr<class SeAudioMaster> generator;
	std::thread dspBuilderThread;
	std::vector< std::pair<int32_t, std::string> > pendingPresets;

private:
	std::unordered_map<int32_t, int32_t> moduleLatencies;
};

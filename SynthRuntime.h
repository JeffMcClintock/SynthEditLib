#ifndef __SynthRuntime_h__
#define __SynthRuntime_h__
 
#include <stdint.h>
#include <mutex>
#include "./iseshelldsp.h"
#include "interThreadQue.h"
#include "./SeAudioMaster.h"
#include "modules/shared/xplatform.h"
#include "IProcessorMessageQues.h"
#include "tinyxml/tinyxml.h"

struct DawPreset;

class my_output_stream_temp : public my_output_stream
{
public:
	virtual void Write( const void* /*lpBuf*/, unsigned int /*nMax*/ ) {}
};

class IShellServices
{
public:
    virtual void onQueDataAvailable() = 0;
    virtual void flushPendingParameterUpdates() = 0;
	virtual void onSetParameter(int32_t handle, int32_t field, RawView rawValue, int voiceId) = 0;
	virtual void EnableIgnoreProgramChange() = 0;
};

class SynthRuntime : public SeShellDsp
{
	IShellServices* shell_ = {};
	bool restartDontRestorePresets{};

public:
	SynthRuntime();
	~SynthRuntime();

	void prepareToPlay(
		IShellServices* shell,
		int32_t sampleRate,
		int32_t maxBlockSize,
		bool runsRealtime);

	void OpenGenerator();

	void setProcessor( IShellServices* vst3Processor )
	{
		shell_ = vst3Processor;
	}
    
	void process(
		int sampleFrames
		, const float* const* inputs
		, float* const* outputs
		, int inChannelCount
		, int outChannelCount
		, int64_t allSilenceFlagsIn
		, int64_t& allSilenceFlagsOut
	);
	void MidiIn( int delta, const unsigned char* midiData, int length )
	{
		generator->MidiIn(delta, midiData, length );
	}
	timestamp_t getSampleClock()
	{
		return generator->SampleClock();
	}
	//void setInputSilent(int input, bool isSilent)
	//{
	//	generator->setInputSilent(input, isSilent);
	//}
	uint64_t getSilenceFlags(int output, int count)
	{
		return generator->getSilenceFlags(output, count);
	}

	// ISeShellDsp support.
    void ServiceDspRingBuffers() override;
	void ServiceDspWaiters2(int sampleframes, int guiFrameRateSamples) override;
	void EnableIgnoreProgramChange() override
	{
		shell_->EnableIgnoreProgramChange();
	}

	virtual void RequestQue( class QueClient* client, bool noWait = false ) override;

	virtual void NeedTempo() override {usingTempo_=true;}

	virtual std::wstring ResolveFilename(const std::wstring& name, const std::wstring& extension) override;
	virtual std::wstring getDefaultPath(const std::wstring& p_file_extension ) override;
	virtual void GetRegistrationInfo(std::wstring& p_user_email, std::wstring& p_serial) override;
	virtual void DoAsyncRestart() override;
	void DoAsyncRestartCleanState();
	void ClearDelaysUnsafe();
	bool NeedsTempo( ){ return usingTempo_; }
	bool isEditor() override { return false; }
	void SetCancellationMode() override {}

	// For VST process side automation.
	// uses a parameter 'tag' to identify the parameter. Might not map directly to parameter vstIndex in case of JUCE
	// because JUCE parameters are forced into strictly sequential indexing.
	void setParameterNormalizedDsp( int timestamp, int paramIndex, float value, int32_t flags )
	{
		assert(generator);
		generator->setParameterNormalizedDsp( timestamp, paramIndex, value, flags );
	}
	void setParameterNormalizedDaw(int timestamp, int32_t paramHandle, float value, int32_t flags)
	{
		assert(generator);
		generator->setParameterNormalizedDaw(timestamp, paramHandle, value, flags);
	}

	void UpdateTempo( my_VstTimeInfo * ti )
	{
		generator->UpdateTempo( ti );
	}
    
	void setPresetUnsafe(DawPreset const* preset);

	int getNumInputs()
	{
		return generator->getNumInputs();
	}
	int getNumOutputs()
	{
		return generator->getNumOutputs();
	}
	bool wantsMidi()
	{
		return generator->wantsMidi();
	}
	bool sendsMidi()
	{
		return generator->sendsMidi();
	}

	class MidiBuffer3* getMidiOutputBuffer()
	{
		return generator->getMidiOutputBuffer();
	}

	int getLatencySamples();
	int32_t SeMessageBox(const wchar_t* msg, const wchar_t* title, int flags) override;
	int RegisterIoModule(ISpecialIoModule*) override { return 1; } // nothing special to do in plugin
	void onSetParameter(int32_t handle, int32_t field, RawView rawValue, int voiceId)  override;
	void dumpPreset(int tag)
	{
#ifdef _DEBUG
		generator->dumpPreset(tag);
#endif
	}
private:

	// Communication pipes Controller<->Processor
	QueuedUsers pendingControllerQueueClients; // parameters waiting to be sent to GUI

	IWriteableQue* MessageQueToGui() override // ISeShellDsp interface
	{
		return peer->MessageQueToGui();
	}
	void ResetMessageQues()
	{
		pendingControllerQueueClients.Reset();
// hmm. was opposit on pc, seems a typo on mac (should have been 'ProcessorToControllerQue_'). mayby not needed.        peer->ControllerToProcessorQue()->Reset();
	}
    
	IProcessorMessageQues* peer = {};
    
public:
	void connectPeer(IProcessorMessageQues* ppeer)
	{
		peer = ppeer;
	}

    void OnSaveStateDspStalled() override;
	std::function<void()> onRestartProcessor = [](){};

private:
	bool usingTempo_;
	int32_t currentPluginLatency = {}; // as at previous initialise
	std::mutex generatorLock;
	bool runsRealtimeCurrent = true;
	TiXmlDocument currentDspXml;
	int32_t sampleRate{};
	int32_t maxBlockSize{};
};

#endif

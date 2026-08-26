#ifndef __SynthRuntime_h__
#define __SynthRuntime_h__
 
#include <atomic>
#include <stdint.h>
#include <mutex>
#include "./iseshelldsp.h"
#include "./SeAudioMaster.h"
#include "modules/shared/xplatform.h"
#include "IProcessorMessageQues.h"
#include "tinyxml/tinyxml.h"
#include "Hosting/message_queues.h"

struct DawPreset;

class my_output_stream_temp : public gmpi::hosting::my_output_stream
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
	virtual void latencyChanged(int newLatency) = 0; // called on the Processor (real-time) thread when async restart changes latency. Implementations must defer to the foreground thread before notifying the DAW.
};

class SynthRuntime : public SeShellDsp
{
	IShellServices* shell_ = {};
	bool restartDontRestorePresets{};
	std::string pendingDocumentXml_; // guarded by generatorLock; see setDocumentXml
	bool documentPending_ = false;   // guarded by generatorLock; forces the next prepareToPlay to rebuild

	// DoAsyncRestart's plugin-runtime half. The standalone app restarts via
	// the fade thread (TriggerRestart); a plugin has no such thread - the
	// host drives every block - so the request is parked here and the block
	// loop consumes it. See takePluginRestartRequest.
	std::atomic<bool> pluginRestartRequested_{ false };

	// True when the HOST drives every block and no fade thread exists to
	// complete a TriggerRestart. Set explicitly by the embedding processor
	// (TIDE), because nothing here can infer it: synth_thread_running is set
	// unconditionally by prepareToPlay - the name lies, it means 'generator
	// open' - so it cannot distinguish the standalone app from a plugin.
	// Measured 2026-08-26: with this false in TIDE, a patch-cable edit's
	// DoAsyncRestart entered the fade machinery, whose completion path never
	// runs in a plugin, and the request silently died (TideSynth E28).
	bool hostDrivenRestart_ = false;

public:
	SynthRuntime();
	~SynthRuntime();

	void prepareToPlay(
		IShellServices* shell,
		int32_t sampleRate,
		int32_t maxBlockSize,
		bool runsRealtime);

	void OpenGenerator();
	void checkLatency();

	// TIDE (TideSynth S12): accept the document from the EDITOR at runtime
	// instead of from the exporter-baked 'dsp.se.xml' bundle resource. Safe to
	// call from any thread, before or after the generator exists: before the
	// first prepareToPlay it just seeds the XML; while running it triggers the
	// existing fade-out / teardown / rebuild / fade-up (DoAsyncRestart), and
	// the resetting branch of process() swaps the document in. Presets are NOT
	// harvested across a document swap - parameter handles need not survive an
	// arbitrary edit, and the editor's document is the state of record.
	void setDocumentXml(const std::string& xml);

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
	void ServiceDspWaiters2(int sampleframes) override;
	void EnableIgnoreProgramChange() override
	{
		shell_->EnableIgnoreProgramChange();
	}

	void RequestQue(gmpi::hosting::QueClient* client, bool noWait = false ) override;

	virtual void NeedTempo() override {usingTempo_=true;}

	virtual std::wstring ResolveFilename(const std::wstring& name, const std::wstring& extension) override;
	virtual std::wstring getDefaultPath(const std::wstring& p_file_extension ) override;
	virtual void GetRegistrationInfo(std::wstring& p_user_email, std::wstring& p_serial) override;
	virtual void DoAsyncRestart() override;

	// The plugin runtime's answer to DoAsyncRestart, polled by the host-driven
	// block loop (TIDE checks it each subProcess). True at most once per
	// request; consuming it arms documentPending_ WITHOUT touching the pending
	// string, so the next prepareToPlay rebuilds from the cached currentDspXml
	// - the document the DSP already holds, not another copy. Host controls in
	// persistAcrossResets (the patch-cable list among them) survive the
	// rebuild, which is the entire point: a re-cabling rebuilds the graph with
	// the NEW cable list and the OLD document.
	bool takePluginRestartRequest();
	void setHostDrivenRestart(bool b) { hostDrivenRestart_ = b; }

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
	gmpi::hosting::QueuedUsers pendingControllerQueueClients; // parameters waiting to be sent to GUI

	gmpi::hosting::IWriteableQue* MessageQueToGui() override // ISeShellDsp interface
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

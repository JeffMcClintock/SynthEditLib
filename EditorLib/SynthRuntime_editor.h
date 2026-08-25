#pragma once

#include "iseshelldsp.h"
#include "UIoManager.h"
#include "SeAudioMaster.h"
#include "tinyxml/tinyxml.h"
#include "IO_base.h"
#include "Hosting/message_queues.h"
#include <vector>
#include "FeedbackTrace.h"

// manages Processor on behalf of the app
class SynthRuntime_editor : public SeShellDsp, public AudioDriverClient
{
protected:
	std::unique_ptr<UIoManager> io_manager;
	std::unique_ptr<SeAudioMaster> generatorBeingbuilt;
	class CSynthEditAppBase* application = {};

	// messages from Audio -> GUI via lock free FIFO.
	gmpi::hosting::QueuedUsers pendingControllerQueueClients; // parameters waiting to be sent to GUI
public:
	gmpi::hosting::QueuedUsers pendingProcessorQueueClients; // parameters waiting to be sent to DSP

protected:
	gmpi::hosting::interThreadQue message_que_dsp_to_ui;
	gmpi::hosting::interThreadQue m_message_que_ui_to_dsp;

	int que_maximum_to_gui = 0;
	int que_maximum_to_dsp = 0;
	ElatencyContraintType latencyConstraint = ElatencyContraintType::Off;
	std::wstring audioDeviceGuid;
	bool done_flag = false; // processing completed and objects destroyed.
	bool shutDownFailedAlready = false;

public:
	class IO_base* audio_driver = {};
	bool cancellationMode = false;

	std::unique_ptr<TiXmlDocument> currentDspXml;
	std::unique_ptr<TiXmlDocument> pendingDspXml;
	std::unordered_map<int64_t, std::string> extraPinDefaultChanges;

	int realtime_flags = {};
	FeedbackTrace feedbackTrace;

	SynthRuntime_editor(class CSynthEditAppBase* papp);

	void Clear() override
	{
		SeShellDsp::Clear();
		pendingDspXml = {};
		currentDspXml = {};
	}

	void Init(
		IO_base* p_audio_driver,
		std::wstring audioDeviceGuid,
		int realtime_flags,
		ElatencyContraintType latencyCompensationType
	);

	bool prepareToPlay();
	void StartBackgroundProcessing();
	void Stop();
	void RunGenerator();
	bool buildFailed();

	void OpenGenerator();

	// ISeShellDsp
	gmpi::hosting::IWriteableQue* MessageQueToGui() override
	{
		return &message_que_dsp_to_ui;
	}
	void ServiceDspRingBuffers() override;
	void ServiceDspWaiters2(int sampleframes) override;
	void RequestQue(gmpi::hosting::QueClient* client, bool noWait = false ) override
	{
		if(noWait)
		{
			client->inQue_ = false;
			auto& que = message_que_dsp_to_ui;
			gmpi::hosting::my_msg_que_output_stream outStream( &que );
			client->getQueMessage(outStream, client->queryQueMessageLength( INT_MAX ) );
			que.Send();
		}
		else
		{
			pendingControllerQueueClients.AddWaiter(client);
		}
	}
	void onSetParameter(int32_t, int32_t, RawView, int) override {} //N/A

	void NeedTempo() override{} // Not supported in editor yet.

	std::wstring ResolveFilename(const std::wstring& name, const std::wstring& extension) override;
	std::wstring getDefaultPath(const std::wstring& p_file_extension) override;
	void GetRegistrationInfo(std::wstring& p_user_email, std::wstring& p_serial) override;
	void DoAsyncRestart() override;
	void DoImmediateRestart() override;

	void OnSaveStateDspStalled() override {} // N/A
	int32_t SeMessageBox(const wchar_t* msg, const wchar_t* title, int flags) override;
	int RegisterIoModule(ISpecialIoModule*) override;
	void EnableIgnoreProgramChange() override {}
	bool isEditor() override { return true; }
	void SetCancellationMode() override
	{
		cancellationMode = true;
	}

	// Track how full the ques are for ui_que_monitor module.
	void GetQueStats( int& max_to_gui, int& max_to_dsp, bool& overflow_to_gui )
	{
		overflow_to_gui = false;
		max_to_gui = que_maximum_to_gui;
		max_to_dsp = que_maximum_to_dsp;
		que_maximum_to_dsp = 0;
		que_maximum_to_gui = 0;
	}

	gmpi::hosting::IWriteableQue* MessageQueToDsp()
	{
		return &m_message_que_ui_to_dsp;
	}
	int32_t sendSdkMessageToAudio(int32_t handle, int32_t id, int32_t size, const void* messageData);

	void serviceQueues();

	// Feed DSP->GUI messages that came from OUTSIDE this runtime's own
	// processor, and hand them to the app immediately.
	//
	// serviceQueues() above drains message_que_dsp_to_ui for the case this
	// class was written for: the editor OWNS the running processor, both
	// live in one object, and the processor writes into that queue directly.
	// A PLUG-IN does not work that way -- its processor is a separate object,
	// and under AUv3 a separate PROCESS -- so its parameter feedback ("ppc"
	// messages: output parameters, meters, scope captures) arrives as bytes
	// carried across by the wrapper, with nothing to put them into. This is
	// that entry point: same queue, same framing, same reader, different
	// source.
	//
	// `data` must be the raw framed bytes exactly as the processor's own
	// queue produced them -- copy them verbatim, do not re-frame. Partial
	// messages are fine: the queue reassembles across calls.
	void receiveDspMessages(const unsigned char* data, int size);

	// The GUI->DSP MIRROR of receiveDspMessages, and the half S12 was missing.
	//
	// A parameter edit (a knob, a button) is already serialised into
	// m_message_que_ui_to_dsp by the patch manager's waiters - in SynthEdit
	// proper the DSP shares the process and polls that queue directly. A
	// plug-in's processor does not: it is a separate object, and under AUv3 a
	// separate process, so the bytes have to be carried. Without that carriage
	// a parameter edit reached the DSP only by rebuilding the whole graph from
	// a pushed document, which is both enormously expensive (one button press
	// tore down and rebuilt every module in the rack) and, measured
	// 2026-08-25, did not even work - the rebuilt engine kept its previous
	// parameter values and ignored the pushed ones.
	//
	// Returns whole messages ready to ship, or false when there is nothing.
	bool takeUiToDspMessages(std::vector<unsigned char>& out);
	bool SynthBuilt() const;
	bool SynthRunning() const;
	bool ProcessingCompleted() const;
	void OnSynthThreadExit();

	// for audio driver
	int SampleRate()
	{
		return static_cast<int>(generator->SampleRate());
	}
	bool isProcessingComplete() override
	{
		return done_flag;
	}
	int64_t SampleClock()
	{
		return generator->SampleClock();
	}
	void UpdateTempo(my_VstTimeInfo* timeInfo)
	{
		generator->UpdateTempo(timeInfo);
	}

	void process(int sampleFrames, const float** inputs, float** outputs, int inChannelCount, int outChannelCount) override;

	std::unordered_map<int64_t, std::string>* getExtraPinDefaultChanges() override
	{
		return &extraPinDefaultChanges;
	}
};


#include <chrono>
#include <thread>
#include <future>
#include "SynthRuntime_editor.h"
#include "SynthEditAppBase.h"
#include "SeAudioMaster.h"
#include "IO_base.h"
#include "SeException.h"
#include "UniqueSnowflake.h"
#include "Hosting/message_queues.h"

using namespace gmpi::hosting;

SynthRuntime_editor::SynthRuntime_editor(CSynthEditAppBase* papp) :
	message_que_dsp_to_ui(SeAudioMaster::AUDIO_MESSAGE_QUE_SIZE) //CI UI_MESSAGE_QUE_SIZE2)
	, m_message_que_ui_to_dsp(SeAudioMaster::AUDIO_MESSAGE_QUE_SIZE)
	, feedbackTrace(-1)
	, application(papp)
{
	#if defined( _DEBUG )
		message_que_dsp_to_ui.isFromGui = true;
	#endif
}

// this should happen one-time-only in plugin.
// in editor it should happen every time user hits 'play' but not on automatic resets (e.g. latency change)
void SynthRuntime_editor::Init(
	IO_base* paudio_driver,
	std::wstring paudioDeviceGuid,
	int prealtime_flags,
//	float preferredSampleRate,
	ElatencyContraintType latencyCompensationType
)
{
//	sampleRate = preferredSampleRate;
	realtime_flags = prealtime_flags;
	audio_driver = paudio_driver;
	audioDeviceGuid = paudioDeviceGuid;
	latencyConstraint = latencyCompensationType;
}

bool SynthRuntime_editor::prepareToPlay()
{
	if (generator)
	{
		assert(false); // sound engine hung on prev run
		return false;
	}

	const bool in_required = (realtime_flags & SER_SOUNDCARD_IN) != 0;
	const bool out_required = (realtime_flags & SER_SOUNDCARD_OUT) != 0;

	{
		assert( !io_manager );
		io_manager = std::make_unique<UIoManager>();

		io_manager->m_midi_driver = application->m_midi_driver.get();
		io_manager->CurrentMidiOutDev = application->settings.CurrentMidiOutDev;
		io_manager->CurrentMidiInDevs = application->settings.CurrentMidiInDevs;
		io_manager->m_error_message_callback = [this](const wchar_t* msg) { application->DeferredMessageBox(msg, 0); };
		
		const bool res = io_manager->Initialise( this, audio_driver, audioDeviceGuid, in_required, out_required );
		if (!res)
		{
			io_manager = {};
			return false;
		}
	}

	const auto sampleRate = audio_driver->getSampleRate();
	generator = std::make_unique<SeAudioMaster>(static_cast<float>(sampleRate), this, latencyConstraint);
	// set block size
	generator->setBufferSize(audio_driver->getBufferSize());

	// regular (per-module) CPU measurement
	const float cpu_clock_rate = 1000000000.f; // aka nanoseconds.
	ug_base::cpu_conversion_const2 = sampleRate / ((float)generator->BlockSize() * cpu_clock_rate); // per block
	ug_base::cpu_conversion_const = sampleRate / ((float)generator->BlockSize() * cpu_clock_rate * generator->cpu_block_rate);

	// Send patch structure to process.
	currentDspXml = std::move(pendingDspXml);

	std::vector<int32_t> mutedContainers; // unused at present. (Waves thing).
	generator->BuildDspGraph(currentDspXml.get(), pendingPresets, mutedContainers);

	runtimeState = eRuntimeState::running;
//	_RPT0(0, "eRuntimeState::running\n");

	return true;
}

bool SynthRuntime_editor::buildFailed()
{
	return runtimeState == eRuntimeState::newDspFailed;
}

void SynthRuntime_editor::OpenGenerator()
{
	generator->Open();
//	generator->CpuFunc();

	// InitialMusicTimeUpdate()
	{
		my_VstTimeInfo timeInfo{};
		timeInfo.tempo = 120.0;
		timeInfo.flags = my_VstTimeInfo::kVstTransportPlaying | my_VstTimeInfo::kVstTempoValid | my_VstTimeInfo::kVstTimeSigValid | my_VstTimeInfo::kVstBarsValid | my_VstTimeInfo::kVstPpqPosValid;
		timeInfo.timeSigNumerator = 4;
		timeInfo.timeSigDenominator = 4;
		timeInfo.flags = my_VstTimeInfo::kVstTransportPlaying;

		generator->UpdateTempo(&timeInfo);
	}

	const bool runsRealtime = io_manager->AudioDriver()->RunsRealTime();
	generator->SetHostControl(HC_PROCESS_RENDERMODE, runsRealtime ? 0 : 2); // from Waves. Mode 0 = "Live", 2 = "Preview" (Offline)
}

// This is the entry/exit point of the background synth thread
void SynthRuntime_editor::RunGenerator()
{
	done_flag = false;

	// create a random random seed for random number generators.
	// This can be overriden later by cancellation test if required.

	// When running cancellation, ensure random number are consistant between runs by resetting random number generator.
	if (cancellationMode)
	{
		srand(0); // todo!!!!, need it to also be cross-platform consistant.
	}
	else
	{
		srand((unsigned)time(nullptr)); // seed randon number for pink noise etc. Must be done on specific thread.
	}

	OpenGenerator();

	// Run UG functions till one tells us we are done
	io_manager->Render();

//	_RPT0(0, "Exiting background thread...\n");

	io_manager->Stop();
	io_manager = {};

	// clear msg que
// not thread-safe.moved.	message_que_dsp_to_ui.Reset();

	pendingControllerQueueClients.Reset();

	generator = {};

	pendingDspXml = {};
	currentDspXml = {};

	// Signal to GUI thread, that DSP has exited.
	my_msg_que_output_stream strm(MessageQueToGui(), UniqueSnowflake::APPLICATION, "done");
	strm << (int32_t)0; // message length.
	strm.Send();
}

#ifdef _WIN32
void SetThreadName( DWORD dwThreadID, char* threadName)
{
	#define MS_VC_EXCEPTION 0x406D1388
  
	#pragma pack(push,8)
	typedef struct tagTHREADNAME_INFO
	{
		DWORD dwType; // Must be 0x1000.
		LPCSTR szName; // Pointer to name (in user addr space).
		DWORD dwThreadID; // Thread ID (-1=caller thread).
		DWORD dwFlags; // Reserved for future use, must be zero.
	} THREADNAME_INFO;
	#pragma pack(pop)

	Sleep(10);
	THREADNAME_INFO info;
	info.dwType = 0x1000;
	info.szName = threadName;
	info.dwThreadID = dwThreadID;
	info.dwFlags = 0;
  
	__try
	{
	RaiseException( MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info );
	}
	__except(EXCEPTION_EXECUTE_HANDLER)
	{
	}
}
#endif

void SynthRuntime_editor::StartBackgroundProcessing()
{
	message_que_dsp_to_ui.Reset(); // seems safest place to assume DSP is not running.

	// Call RunGenerator() in new thread
	std::thread backgroundThread(
		[this]
		{
			RunGenerator();
		}
	);

#ifdef _WIN32
	SetThreadPriority((HANDLE)backgroundThread.native_handle(), THREAD_PRIORITY_ABOVE_NORMAL);

	// Set thread name for debugging
	SetThreadDescription((HANDLE) backgroundThread.native_handle(), L"SynthEdit Audio Process");
#endif

	backgroundThread.detach();


/*
* NOPE: race condition. (on a fast run) generator can be null already
	// Ensure flag is set (else toggle-run can get confused and crash (hold down space-bar to test)
	// synth_thread_started PREVENTS hang on very fast runs...WHEN RENDERING DONE before this statement reached.
	int timeout = 0;

	while( !generator->synth_thread_started && timeout++ < 300 ) // 3 sec timeout
		Sleep(10);
*/

	shutDownFailedAlready = false;
}

void SynthRuntime_editor::Stop()
{
#ifdef _MFC_VER
	CWaitCursor wait; // show 'busy' cursor
#else
	// WIN UI way???
#endif

	//_RPT0(_CRT_WARN, "SynthRuntime_editor::Stop()\n" );
	// 3 Possible conditions:
	// 1) synth completely stopped, no generator. Nothing to do
	// 2) synth stopped, but generator not destroyed yet. Call Onthreadexit to destroy generator
	// 3) Synth Running. Signal it to stop, then call synththreadexit.
	if( !SynthBuilt() )
	{
		//_RPT0(_CRT_WARN, "CSynthEditAppBase::Stop(): not running\n" );
		return;
	}

	if( !SynthRunning() )
	{
		//_RPT0(_CRT_WARN, "CSynthEditAppBase::Stop(): generator exists, but not running. Attempting shutdown.\n" );
	}

	// signal controller to shutdown
	//	_RPT0(_CRT_WARN, "\ncontroller.stop(): Waiting synth thread shutdown...\n" );
	generator->TriggerShutdown();

	if (shutDownFailedAlready)
		return;

	int timeout = 1000; // 10 second
	while( generator && timeout-- > 0)
	{
		application->OnTimer();// empty the mesage que (in case ug blocking on full que)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}

	if( timeout < 1 )
	{
//		_RPT0(_CRT_WARN, "\n!!!SYNTH THREAD WON'T STOP!!!!!\n" );
		SeMessageBox( (L"Sound Engine not responding. Please Save and Exit"), L"", MB_ICONEXCLAMATION );
		shutDownFailedAlready = true;
		return;
	}

	OnSynthThreadExit(); // yes it will also be called indirectly, but not immediately, so if UI trys to restart, generator may not be deleted in time
}

void rebuildDsp(
	  SeAudioMaster* generator
	, TiXmlDocument* currentDspXml
	, std::vector< std::pair<int32_t, std::string> >& pendingPresets
	, std::atomic<eRuntimeState>& runtimeState
	, FeedbackTrace& returnFeedbackError
	, std::unordered_map<int64_t, std::string>& extraPinDefaultChanges
)
{
//	_RPT0(0, "backGroundRebuildDsp:: start\n");

	try
	{
		// Send patch structure to process.
		std::vector<int32_t> mutedContainers; // unused at present. (Waves thing).
		generator->BuildDspGraph(currentDspXml, pendingPresets, mutedContainers);
		generator->ApplyPinDefaultChanges(extraPinDefaultChanges);

//		_RPT0(0, "backGroundRebuildDsp:: done\n");
//		_RPT0(0, "eRuntimeState::newDspReady\n");
		runtimeState.store(eRuntimeState::newDspReady, std::memory_order_release);
	}
	catch (FeedbackTrace* e)
	{
		returnFeedbackError = *e;
//		_RPT0(0, "eRuntimeState::newDspFailed\n");
		runtimeState.store(eRuntimeState::newDspFailed, std::memory_order_release);
	}
	catch (SeException*)
	{
		runtimeState.store(eRuntimeState::newDspFailed, std::memory_order_release);
	}
}

// see also SynthRuntime::process()
void SynthRuntime_editor::process(int sampleFrames, const float** inputs, float** outputs, int inChannelCount, int outChannelCount)
{
	const auto myState = runtimeState.load(std::memory_order_acquire);

	switch (myState)
	{
	case eRuntimeState::idling:
	case eRuntimeState::stopped: // ASIO is a bit async, can call here after I'ved asked it to stop
	{
//		_RPT0(0, "eRuntimeState::idling\n");
		// output silence
		for (int i = 0; i < outChannelCount; ++i)
		{
			auto* dest = outputs[i];
			std::fill(dest, dest + sampleFrames, 0.0f);
		}
	}
	break;

	case eRuntimeState::newDspFailed:
	{
		if(dspBuilderThread.joinable())
			dspBuilderThread.join();

		generator->processingState = audioMasterState::Stopped;
//		_RPT0(0, "audioMasterState::Stopped\n");
		done_flag = true;
	}
	break;

	case eRuntimeState::newDspReady:
	{
//		_RPT0(0, "eRuntimeState::newDspReady\n");
		if (dspBuilderThread.joinable())
			dspBuilderThread.join();

//		_RPT0(0, "eRuntimeState::running\n");
		runtimeState = eRuntimeState::running;

		const bool runsRealtime = io_manager->AudioDriver()->RunsRealTime();

//		_RPT0(0, "Restart - set preset/s\n");
		generator->setPresetsState(pendingPresets);
		pendingPresets.clear();

		generator->SetHostControl(HC_PROCESS_RENDERMODE, runsRealtime ? 0 : 2); // from Waves. Mode 0 = "Live", 2 = "Preview" (Offline)

		OpenGenerator();

//		_RPT0(0, "eRuntimeState::running...\n");
	}
	[[fallthrough]];

	case eRuntimeState::running:
	{
		generator->DoProcess_editor(sampleFrames, inputs, outputs, inChannelCount, outChannelCount);
	}
	break;

	case eRuntimeState::resetting:
	{
		// Switch out updated DSP XML if nesc

		// a new thread, when done, reengage with soundcard.

		// clear msg que
		// race condition message_que_dsp_to_ui.Reset();
		pendingControllerQueueClients.Reset();

		const auto generatorStateWas = generator->processingState.load();

		// start chain reaction of sound object destruction
		generator->Close();

		if (generatorStateWas == audioMasterState::AsyncRestart)
		{
			if (pendingDspXml)
			{
				currentDspXml = std::move(pendingDspXml);
//					if (!isReinit)
				// document may have been altered/loaded since last run. ensure no 'hanging' modules or old data in here.
				// but if we're restarting due a latency change, then we need to KEEP the latency table.
				GetModuleLatencies().clear();

				extraPinDefaultChanges.clear();
			}
			else
			{
//				_RPT0(0, "Restart - get preset\n");

				// we're restarting independent of the document
				pendingPresets.clear();
				const bool saveExtraState = true;
				generator->getPresetsState(pendingPresets, saveExtraState);
			}

			io_manager->OnRebuildDsp();

//			_RPT0(0, "eRuntimeState launching rebuild thread\n");
			runtimeState = eRuntimeState::idling;
//			_RPT0(0, "eRuntimeState::idling\n");

			const auto sampleRate = audio_driver->getSampleRate();

			generator = std::make_unique<SeAudioMaster>(static_cast<float>(sampleRate), this, latencyConstraint);
			generator->setBufferSize(audio_driver->getBufferSize());

			if (io_manager->AudioDriver()->RunsRealTime())
			{
				dspBuilderThread = std::thread(
					[this]
				{
					rebuildDsp(
						generator.get()
						, currentDspXml.get()
						, pendingPresets
						, runtimeState
						, feedbackTrace
						, extraPinDefaultChanges
					);
				}
				);
			}
			else
			{
				// in offline mode, block while reloading.
				rebuildDsp(
					generator.get()
					, currentDspXml.get()
					, pendingPresets
					, runtimeState
					, feedbackTrace
					, extraPinDefaultChanges
				);
			}
		}
		else
		{
			assert(generatorStateWas == audioMasterState::Stopping);
			
			generator->processingState = audioMasterState::Stopped;
//			_RPT0(0, "audioMasterState::Stopped\n");
//			_RPT0(0, "eRuntimeState::stopped\n");
			runtimeState.store(eRuntimeState::stopped, std::memory_order_release);

			// we're done
			done_flag = true; // cause audio driver to stop immediatly (important for unit tests).
		}
	}
	break;

	}
}

// This is called when background thread decide to stop
// OR from Stop(), or when build fails because of feedback error etc.
// IF audio thread exited on it's own accord, generator is already deleted. Otherwise (error building graph) need to delete it here.
void SynthRuntime_editor::OnSynthThreadExit()
{
	/*
	// audio thread has exited.
	if( !generator )
	{
		return;
	}
*/
// TODO remove in SE 1.6 becuase we now do this more reliably in RunGenerator()
// clear msg que
// not thread safe message_que_dsp_to_ui.Reset();
pendingControllerQueueClients.Reset();

	// start chain reaction of sound object destruction
	generator = {};
	io_manager = {};

// too soon	application->OnSynthStopped(); // update power button.

	// Running automatic benchmarking?
	if (SeAudioMaster::profileBlockSize && SeAudioMaster::profileBlockSize < 500)
	{
		SeAudioMaster::profileBlockSize += 8;

		application->CSynthEditAppBase::Run();
	}
}

// Called from DSP thread!
void SynthRuntime_editor::ServiceDspRingBuffers()
{
	que_maximum_to_dsp = (std::max)( que_maximum_to_dsp, m_message_que_ui_to_dsp.readyBytes() );
	m_message_que_ui_to_dsp.pollMessage(generator.get()); // read messages from GUI.
}

void SynthRuntime_editor::ServiceDspWaiters2(int sampleframes)
{
	pendingControllerQueueClients.ServiceWaitersIncremental(&message_que_dsp_to_ui, sampleframes);
}

int32_t SynthRuntime_editor::sendSdkMessageToAudio(int32_t handle, int32_t id, int32_t size, const void* messageData)
{
	// TODO:: All uses of m_message_que_ui_to_dsp should check AudioMaster()->done_flag, else extended delay results while que times out repeatedly
	if (generator && !done_flag)
	{
		// discard any too-big message.
		const auto totalMessageSize = 4 * static_cast<int>(sizeof(int)) + size;
		if(totalMessageSize > m_message_que_ui_to_dsp.freeSpace())
			return gmpi::MP_FAIL;

		my_msg_que_output_stream strm(&(m_message_que_ui_to_dsp), handle, "sdk");
		strm << (int)(sizeof(int) + sizeof(int) + size); // message length.
		strm << (int)id; // user provided msg_id
		strm << (int)size;
		strm.Write(messageData, size);
		strm.Send();

		return gmpi::MP_OK;
	}

	return gmpi::MP_FAIL;
}


std::wstring SynthRuntime_editor::ResolveFilename(const std::wstring& name, const std::wstring& extension)
{
	return application->ResolveFilename(name, extension);
}
std::wstring SynthRuntime_editor::getDefaultPath(const std::wstring& p_file_extension )
{
	return application->getDefaultPath(p_file_extension);
}
void SynthRuntime_editor::GetRegistrationInfo(std::wstring& p_user_email, std::wstring& p_serial)
{
	return application->GetRegistrationInfo(p_user_email, p_serial);
}

int32_t SynthRuntime_editor::SeMessageBox(const wchar_t* msg, const wchar_t* title, int flags)
{
	return application->SeMessageBox(msg, title, flags);
}

int SynthRuntime_editor::RegisterIoModule(ISpecialIoModule* m)
{
	return io_manager->RegisterIoModule(m);
}

void SynthRuntime_editor::serviceQueues() // message-thread
{
	que_maximum_to_gui = (std::max)( que_maximum_to_gui, message_que_dsp_to_ui.readyBytes() );

	{
		// my_msg_que_output_stream outStream(&m_message_que_ui_to_dsp);
		//if(pendingProcessorQueueClients.ServiceWaiters(outStream, m_message_que_ui_to_dsp.freeSpace(), m_message_que_ui_to_dsp.totalSpace()))
		//{
		//	outStream.Send();
		//}

		pendingProcessorQueueClients.ServiceWaitersIncremental(&m_message_que_ui_to_dsp, 100000);
	}
	message_que_dsp_to_ui.pollMessage(application);
}

// See the header for why a plug-in needs this and serviceQueues() does not
// cover it. Push-then-Send-then-poll is the same three steps the VST3
// wrapper's controller does with its own copy of this queue
// (Controller_VST3::notify pushes and Sends, ::onTimer polls); doing all
// three here keeps the caller from needing a timer of its own.
void SynthRuntime_editor::receiveDspMessages(const unsigned char* data, int size)
{
	if (!data || size <= 0)
		return;

	message_que_dsp_to_ui.pushString(size, data);
	message_que_dsp_to_ui.Send();
	message_que_dsp_to_ui.pollMessage(application);
}

// See the header. Service first, then take: the waiters hold the parameter
// updates and only writing them into the queue makes them bytes.
//
// Everything ready is taken in one go, which keeps whole-message framing
// intact - ServiceWaitersIncremental only ever Sends complete messages, so a
// queue's ready bytes always end on a message boundary. That matters because
// the caller ships these through a BLOB PARAMETER, which is last-writer-wins:
// a torn message would desynchronise the receiving queue permanently, the
// exact failure the DSP->GUI direction was fixed for on 2026-08-25.
bool SynthRuntime_editor::takeUiToDspMessages(std::vector<unsigned char>& out)
{
	pendingProcessorQueueClients.ServiceWaitersIncremental(&m_message_que_ui_to_dsp, 100000);

	const int ready = m_message_que_ui_to_dsp.readyBytes();
	if (ready <= 0)
		return false;

	out.resize(static_cast<size_t>(ready));
	m_message_que_ui_to_dsp.popString(ready, out.data());
	return true;
}

void SynthRuntime_editor::DoAsyncRestart()
{
//	_RPT0(_CRT_WARN, "FADE - SynthRuntime_editor::DoAsyncRestart()\n" );
	// When changing polyphony etc we need to rebuild the DSP graph,
	// however it's not nesc to stall GUI by polling for the audio fade-out to complete.
	// Step one is to trigger the fadeout, step two is wait for signal from DSP to call OnFadeOutComplete();

	if( SynthRunning() )
	{
		generator->TriggerRestart();
	}
}

void SynthRuntime_editor::DoImmediateRestart()
{
//	_RPT0(0, "SynthRuntime_editor::DoImmediateRestart()\n");

	// ASIO driver control-panel has e.g. changed sample rate and is no longer running. No time to fade-out.
	// assume not thread-safe.

	// assume callbacks have suddenly halted.
	// but set generator to freewheel, just in case some driver continues to call the callback.
	runtimeState.store(eRuntimeState::stopped, std::memory_order_release);

	done_flag = true; // cause IO_ASIO::Render() to exit in near future

	application->DoImmediateRestartAsync();
}

bool SynthRuntime_editor::SynthBuilt() const
{
	return generator.get() != nullptr;
}

bool SynthRuntime_editor::SynthRunning() const
{
	return generator && generator->synth_thread_running;
}

bool SynthRuntime_editor::ProcessingCompleted() const
{
	return !SynthRunning();// && !user_shutdown_flag;
}

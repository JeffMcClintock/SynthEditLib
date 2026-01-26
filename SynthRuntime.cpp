#include <string>
#include <fstream>
#include <math.h>
#include "SynthRuntime.h"
#include "./SeAudioMaster.h"
#include "./midi_defs.h"
#include "ug_patch_automator.h"
#include "./UniqueSnowflake.h"
#include "module_info.h"
#include "BundleInfo.h"
#include "./modules/se_sdk3_hosting/Controller.h"
#include "UgDatabase.h"
#include "mfc_emulation.h"
#include "SeException.h"
#include "FeedbackTrace.h"

using namespace std;
using namespace gmpi::hosting;

SynthRuntime::SynthRuntime() :
	usingTempo_(false)
{
}

void SynthRuntime::prepareToPlay(
	IShellServices* shell,
	int32_t psampleRate,
	int32_t pmaxBlockSize,
	bool runsRealtime)
{
	shell_ = shell;
	sampleRate = psampleRate;
	maxBlockSize = pmaxBlockSize;

	pendingControllerQueueClients.setSampleRate(psampleRate);

	// this can be called multiple times, e.g. when performing an offline bounce.
	// But we need to rebuild the DSP graph from scratch only when something fundamental changes.
	// e.g. sample-rate, block-size, polyphony, latency compensation, patch cables.

	const bool mustReinitilize =
		generator == nullptr ||
		generator->SampleRate() != sampleRate ||
		generator->BlockSize() != generator->CalcBlockSize(maxBlockSize) ||
		(!runsRealtime && runsRealtimeCurrent);	// i.e. we switched *to* an offline render. need to ensure cancellation is consistant.
//		reinitializeFlag;

//	reinitializeFlag = false;

	if (mustReinitilize)
	{
		ModuleFactory()->RegisterExternalPluginsXmlOnce(nullptr);

		// cache xml document to save time re-parsing on every restart.
		if (!currentDspXml.RootElement())
		{
			auto bundleinfo = BundleInfo::instance();
			auto dspXml = bundleinfo->getResource("dsp.se.xml");
			if (!dspXml.empty() && '<' != dspXml[0])
			{
				Scramble(dspXml);
			}

			currentDspXml.Parse(dspXml.c_str());
			if (currentDspXml.Error())
			{
#if defined( SE_EDIT_SUPPORT )
				std::wostringstream oss;
				oss << L"Module XML Error: [SynthEdit.exe]" << currentDspXml.ErrorDesc() << L"." << currentDspXml.Value();
				getShell()->SeMessageBox(oss.str().c_str(), L"", MB_OK | MB_ICONSTOP);
#else
#if defined (_DEBUG)
				// Get error information and break.
				TiXmlNode* e = currentDspXml.FirstChildElement();
				while (e)
				{
					_RPT1(_CRT_WARN, "%s\n", e->Value());
					TiXmlElement* pElem = e->FirstChildElement("From");
					if (pElem)
					{
						const char* from = pElem->Value();
					}
					e = e->LastChild();
				}
				assert(false);
#endif
#endif
			}
		}

		std::lock_guard<std::mutex> x(generatorLock);

//		_RPT3(_CRT_WARN, "AudioMaster rebuilt %x, SR=%d thread = %x\n", this, sampleRate, (int)GetCurrentThreadId());
		std::vector< std::pair<int32_t, std::string> > pendingPresets;
		if(generator)
		{
			//            _RPT0(CRT_WARN,"\n=========REINIT DSP=============\n");

			// Retain current preset (including non-stateful) when changing sample-rate etc.
			// JUCE will load a preset (causing updates in DSP queue) then immediatly reset the processor.
			// In this case, we need to ensure Processor has latest preset from DAW.
			// Action any waiting parameter updates on GUI->DSP queue.
			ServiceDspRingBuffers();

			// ensure preset from DAW is up-to-date.
			generator->HandleInterrupt();

			// assert(!generator->interrupt_setchunk_);

			const bool saveExtraState = true;
			generator->getPresetsState(pendingPresets, saveExtraState);

			generator->Close();

			// can't leave pointers to deleted objects around waiting for Que.
			ResetMessageQues();

			generator = {};
		}

		// BUILD SYNTHESIZER GRAPH.
		generator = std::make_unique<SeAudioMaster>( (float) sampleRate, this, BundleInfo::instance()->getPluginInfo().latencyConstraint);
		generator->setBlockSize(generator->CalcBlockSize(maxBlockSize));

		std::vector<int32_t> mutedContainers; // unused at preset. (Waves thing).
		generator->BuildDspGraph(&currentDspXml, pendingPresets, mutedContainers);

#if 0
		// Apply preset before Open(), else gets delayed by 1 block causing havok with BPM Clock (receives BPM=0 for 1st block).
		if (!presetChunk.empty())
		{
			generator->setPresetState_UI_THREAD(presetChunk, false);
			assert(reinitializeFlag == false); // loading prior preset should not have changed any persistant host-controls.
		}
#endif
		generator->Open();

		generator->synth_thread_running = true;

		const auto newLatencySamples = generator->getLatencySamples();
		if(currentPluginLatency != newLatencySamples)
		{
			currentPluginLatency = newLatencySamples;

			my_msg_que_output_stream strm( MessageQueToGui(), (int)UniqueSnowflake::APPLICATION, "ltnc");
			strm << (int)0; // message length.
			strm.Send();
		}
	}

	// this can change regardless of if we reinit or not.
	generator->SetHostControl(HC_PROCESS_RENDERMODE, runsRealtime ? 0 : 2);
	runsRealtimeCurrent = runsRealtime;
	runtimeState = eRuntimeState::running;

#ifdef _DEBUG
	generator->dumpPreset(0);
#endif
}

SynthRuntime::~SynthRuntime()
{
//    _RPT2(_CRT_WARN, "~SynthRuntime() %x thread = %x\n", this, (int)GetCurrentThreadId());
    if( generator)
    {
        generator->Close();
		generator = {};
    }
}

void SynthRuntime::OpenGenerator()
{
	generator->Open();
// editor only	generator->CpuFunc();

	/* TODO ensure this happens
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
	*/

//	const bool runsRealtime = io_manager->AudioDriver()->RunsRealTime();
	generator->SetHostControl(HC_PROCESS_RENDERMODE, runsRealtimeCurrent ? 0 : 2); // from Waves. Mode 0 = "Live", 2 = "Preview" (Offline)
}

void rebuildDsp(
	  SeAudioMaster* generator
	, TiXmlDocument* currentDspXml
	, std::vector< std::pair<int32_t, std::string> >& pendingPresets
	, std::atomic<eRuntimeState>& runtimeState
//	, FeedbackTrace& returnFeedbackError
)
{
	_RPT0(0, "backGroundRebuildDsp:: start\n");

	try
	{
		// Send patch structure to process.
		std::vector<int32_t> mutedContainers; // unused at present. (Waves thing).
		generator->BuildDspGraph(currentDspXml, pendingPresets, mutedContainers);
		// generator->ApplyPinDefaultChanges(extraPinDefaultChanges);

		_RPT0(0, "backGroundRebuildDsp:: done\n");
		_RPT0(0, "set eRuntimeState::newDspReady\n");
		runtimeState.store(eRuntimeState::newDspReady, std::memory_order_release);
	}
	catch (FeedbackTrace* e)
	{
		// returnFeedbackError = *e;
		_RPT0(0, "set eRuntimeState::newDspFailed\n");
		runtimeState.store(eRuntimeState::newDspFailed, std::memory_order_release);
	}
	catch (SeException*)
	{
		runtimeState.store(eRuntimeState::newDspFailed, std::memory_order_release);
	}
}

void SynthRuntime::process(
	  int sampleFrames
	, const float* const* inputs
	, float* const* outputs
	, int inChannelCount
	, int outChannelCount
	, int64_t allSilenceFlagsIn
	, int64_t& allSilenceFlagsOut
)
{
	const auto myState = runtimeState.load(std::memory_order_acquire);

	switch (myState)
	{
	case eRuntimeState::idling:
	case eRuntimeState::stopped: // ASIO is a bit async, can call here after I'ved asked it to stop
	{
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
		if (dspBuilderThread.joinable())
			dspBuilderThread.join();

		generator->state = audioMasterState::Stopped;
		_RPT0(0, "audioMasterState::Stopped\n");
	}
	break;

	case eRuntimeState::newDspReady:
	{
		_RPT0(0, "eRuntimeState::newDspReady\n");
		if (dspBuilderThread.joinable())
			dspBuilderThread.join();

		_RPT0(0, "eRuntimeState::running\n");
		runtimeState = eRuntimeState::running;

		_RPT0(0, "Restart - set preset/s\n");
		generator->setPresetsState(pendingPresets);
		pendingPresets.clear();

		OpenGenerator();

#ifdef _DEBUG
		generator->dumpPreset(1);
#endif

		// customisation point. Optimus loads a preset here.
		onRestartProcessor();

#ifdef _DEBUG
		generator->dumpPreset(2);
#endif

		_RPT0(0, "eRuntimeState::running...\n");
	}
	[[fallthrough]];

	case eRuntimeState::running:
	{
		// silence flags
		for (int i = 0; i < inChannelCount; ++i)
		{
			const bool isSilent = 0 != (allSilenceFlagsIn & 1);
			generator->setInputSilent(i, isSilent);
			allSilenceFlagsIn = allSilenceFlagsIn >> 1;
		}

		generator->DoProcess_plugin(
			  sampleFrames
			, inputs
			, outputs
			, inChannelCount
			, outChannelCount
		);

		allSilenceFlagsOut = generator->getSilenceFlags(0, outChannelCount);
	}
	break;

	case eRuntimeState::resetting:
	{
		// Switch out updated DSP XML if nesc

		// a new thread, when done, reengage with soundcard.

		// clear msg que
		// race condition message_que_dsp_to_ui.Reset();
		pendingControllerQueueClients.Reset();

		const auto generatorStateWas = generator->state.load();

		// start chain reaction of sound object destruction
		generator->Close();

		if (generatorStateWas == audioMasterState::AsyncRestart)
		{
#if 0 // editor only
			if (pendingDspXml)
			{
				currentDspXml = std::move(pendingDspXml);

				GetModuleLatencies().clear();

				extraPinDefaultChanges.clear();
			}
			else
#endif
			pendingPresets.clear();
			if(!restartDontRestorePresets)
			{
				_RPT0(0, "Restart - get preset\n");

				// we're restarting independent of the document
				const bool saveExtraState = true;
				generator->getPresetsState(pendingPresets, saveExtraState);
			}
			restartDontRestorePresets = false; // back to normal behaviour.

//				io_manager->OnRebuildDsp();

			_RPT0(0, "eRuntimeState launching rebuild thread\n");
			runtimeState = eRuntimeState::idling;
			_RPT0(0, "eRuntimeState::idling\n");

//				const auto sampleRate = audio_driver->getSampleRate();

			generator = std::make_unique<SeAudioMaster>(static_cast<float>(sampleRate), this, BundleInfo::instance()->getPluginInfo().latencyConstraint);
			//generator->setBufferSize(audio_driver->getBufferSize());
			generator->setBlockSize(generator->CalcBlockSize(maxBlockSize));

			if (runsRealtimeCurrent) //io_manager->AudioDriver()->RunsRealTime())
			{
				dspBuilderThread = std::thread(
					[this]
					{
						rebuildDsp(
							  generator.get()
							, &currentDspXml
							, pendingPresets
							, runtimeState
//								, feedbackTrace
//								, extraPinDefaultChanges
						);
					}
				);
			}
			else
			{
				// in offline mode, block while reloading.
				rebuildDsp(
					  generator.get()
					, &currentDspXml
					, pendingPresets
					, runtimeState
//						, feedbackTrace
//						, extraPinDefaultChanges
				);
			}
		}
		else
		{
			assert(generatorStateWas == audioMasterState::Stopping);

			generator->state = audioMasterState::Stopped;
			_RPT0(0, "audioMasterState::Stopped\n");
			_RPT0(0, "eRuntimeState::stopped\n");
			runtimeState.store(eRuntimeState::stopped, std::memory_order_release);

			// we're done
			// done_flag = true; // cause audio driver to stop immediatly (important for unit tests).
		}
	}
	break;

	}
}

void SynthRuntime::ServiceDspWaiters2(int sampleframes)
{
	pendingControllerQueueClients.ServiceWaitersIncremental(MessageQueToGui(), sampleframes);
	peer->Service();
}

// Periodic poll of parameter update messages from GUI.
// GUI >> DSP.
void SynthRuntime::ServiceDspRingBuffers()
{
	peer->ControllerToProcessorQue()->pollMessage(generator.get());
}

// Send VU meter value to GUI.
void SynthRuntime::RequestQue( QueClient* client, bool noWait )
{
	pendingControllerQueueClients.AddWaiter(client);
}

std::wstring SynthRuntime::ResolveFilename(const std::wstring& name, const std::wstring& extension)
{
	std::wstring l_filename = name;
	std::wstring file_ext;
	std::wstring fileType = extension;
	file_ext = GetExtension(l_filename);

	// Attempt to determin file type. First by supplied extension, then by examining filename.
	if( fileType.empty() )
	{
		if( !file_ext.empty() )
		{
			fileType = file_ext;
		}
	}

	// Add stock file extension if filename has none.
	if( file_ext.empty() )
	{
		if( extension.empty() )
		{
			return l_filename;
		}

		//		file_ext = extension;
		l_filename += (L".") + extension;
	}

	// Is this a relative or absolute path?
#ifdef _WIN32
	if( l_filename.find(':') == string::npos )
#else
    if( l_filename.size() > 1 && l_filename[0] != '/' )
#endif
	{
		std::wstring default_path = getDefaultPath( fileType );
		l_filename = combine_path_and_file( default_path, l_filename );
	}

	// !!may need to search local directory too
	return l_filename;
}
//extern HINSTANCE ghInst;

std::wstring SynthRuntime::getDefaultPath(const std::wstring& p_file_extension )
{
    return BundleInfo::instance()->getResourceFolder();
}

void SynthRuntime::GetRegistrationInfo(std::wstring& p_user_email, std::wstring& p_serial)
{
	p_user_email = Utf8ToWstring(BundleInfo::instance()->getPluginInfo().vendorName);
	p_serial = L"Unknown";
}

void SynthRuntime::DoAsyncRestart()
{
	// old syncronous method: reinitializeFlag = true;

	// When changing polyphony etc we need to rebuild the DSP graph,
	// however it's not nesc to stall GUI by polling for the audio fade-out to complete.
	// Step one is to trigger the fadeout, step two is wait for signal from DSP to call OnFadeOutComplete();

	if (generator && generator->synth_thread_running)
	{
		generator->TriggerRestart();
	}
}

// restart the processor same as above, but don't attempt to restore it's state. Just stick with default state.
// For Optimus.
void SynthRuntime::DoAsyncRestartCleanState()
{
	if (generator && generator->synth_thread_running)
	{
		restartDontRestorePresets = true;
		generator->TriggerRestart();
	}
}

void SynthRuntime::ClearDelaysUnsafe()
{
	std::lock_guard<std::mutex> x(generatorLock);

	if (generator)
	{
		generator->ClearDelaysUnsafe();
	}
}

void SynthRuntime::OnSaveStateDspStalled()
{
//#if defined(SE_TAR GET_AU) && !TARGET_OS_IPHONE
    // Action any waiting parameter updates from AudioUnit.
    shell_->flushPendingParameterUpdates();
    
    // Action any waiting parameter updates on GUI->DSP queue.
    ServiceDspRingBuffers();
//#endif
}

int SynthRuntime::getLatencySamples()
{
	// ! FL Studio calls this from foreground thread. lock in case generator restarting.
	std::lock_guard<std::mutex> x(generatorLock);

	// _RPT1(0, "SynthRuntime::getLatencySamples() -> %d\n", currentPluginLatency);
	return currentPluginLatency;
}

int32_t SynthRuntime::SeMessageBox(const wchar_t* msg, const wchar_t* title, int flags)
{
	return 0;
}

void SynthRuntime::onSetParameter(int32_t handle, int32_t field, RawView rawValue, int voiceId)
{
	shell_->onSetParameter(handle, field, rawValue, voiceId);
}

void SynthRuntime::setPresetUnsafe(DawPreset const* preset)
{
	std::lock_guard<std::mutex> x(generatorLock); // protect against setting preset during a restart of the processor (else preset gets lost).

#if 0 //def _DEBUG
	{
		auto xml = preset->toString(0);
		xml = xml.substr(0, 500);

		_RPTN(0, "\nSynthRuntime::setPresetUnsafe()\n %s\n\n", xml.c_str());
	}
#endif

	if (!generator)
		return;

	// TODO check behaviour during DSP restart
	generator->interrupt_preset_.store(preset, std::memory_order_release);
	generator->TriggerInterrupt();
}

#pragma warning(disable : 4996) //swprintf' was declared deprecated

#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif
#include <math.h>
#include <algorithm>
#include <sstream>
#include "SeAudioMaster.h"
#include "ug_container.h"
#include "UniqueSnowflake.h"
#include "iseshelldsp.h"
#include "ULookup.h"
#include "./midi_defs.h"
#include "./UMidiBuffer2.h"
#include "./ug_patch_automator.h"
#include "./IDspPatchManager.h"
#include "ug_oversampler.h"
#include "denormal_fixer.h"
#include "./modules/shared/xplatform.h"
#include "./modules/shared/xp_critical_section.h"
#include "UgDebugInfo.h"
#include "UgDatabase.h"
#include "ug_io_mod.h"
#include "USampBlock.h"
#include "my_msg_que_input_stream.h"
#include "my_msg_que_output_stream.h"
#include "ug_event.h"
#include "ug_voice_splitter.h"
#include "tinyxml/tinyxml.h"
#include "ULookup.h"
#include "ug_patch_param_setter.h"
#include "dsp_patch_parameter_base.h"
#include "ug_oversampler_in.h"
#include "mfc_emulation.h"
#include "ProcessorStateManager.h"

#if defined(CANCELLATION_TEST_ENABLE) || defined(CANCELLATION_TEST_ENABLE2)
#include "conversion.h"
#include "ug_oscillator2.h"
#include "BundleInfo.h"
#endif

using namespace std;

#define MAX_DEBUG_BUFFERS 40
// 16k buffer
#define AUTOMATION_MESSAGE_QUE_SIZE 0x800

// value experimentally optimised. Less than 90 got slower, and more than 120
#define OPTIMUM_BLOCK_SIZE 96

// produce stats for various block sizes. writes a file to C:\temp\profile.txt
// // see also CSynthEditAppBase::OnSynthThreadExit()
// #define PROFILE_VARIOUS_BLOCK_SIZES

#ifdef PROFILE_VARIOUS_BLOCK_SIZES
int SeAudioMaster::profileBlockSize = 8;
#else
int SeAudioMaster::profileBlockSize = 0; // 'Off'
#endif

// SE 1.1 try to optimize for bigger structures/ less memory.
//#define OPTIMUM_BLOCK_SIZE 64 // - CPU Hit too much, about 10%

/* test results P4 2.4 GHz benchmark.se1
bs	time
4096	19.13
2048	16.94
1024	14.97
512		13.84
256		13.5
128		13.66
64		14.16
32		15.03
16		16.5
*/
LookupTables SeAudioMaster::lookup_tables;

gmpi_sdk::CriticalSectionXp audioMasterLock_;

LookupTables::~LookupTables()
{
	// clear Database
	for( auto it = begin() ; it != end() ; ++it )
	{
		lookup_table_entry* tableEntry = *it;
		delete tableEntry;
	}
}

void ModuleContainer::SortAll(std::vector<ug_base*>& nonExecutingModules)
{
	std::sort(modules.begin(), modules.end(),
		[=](const EventProcessor* a, const EventProcessor* b) -> bool
	{
		if (a->GetFlag(UGF_NEVER_EXECUTES) != b->GetFlag(UGF_NEVER_EXECUTES))
			return a->GetFlag(UGF_NEVER_EXECUTES) < b->GetFlag(UGF_NEVER_EXECUTES);

		if (a->SortOrder != b->SortOrder)
			return a->SortOrder < b->SortOrder;

		// MIDI-CV legacy functionality requires "main" MIDI-CV execute before clones.
		auto uga = dynamic_cast<const ug_base*>(a);
		auto ugb = dynamic_cast<const ug_base*>(b);
		return uga->pp_voice_num < ugb->pp_voice_num;
	});

	int j = 0;
	for (auto m : modules)
	{
		m->moduleContainerIndex = j++;
/* diagnostic info. Print module counts.
		{
			auto ug = dynamic_cast<ug_base*>(m);
			wstring res;
			if (ug->getModuleType())
			{
				res = ug->getModuleType()->GetName();
				if (res.empty())
				{
					res = ug->getModuleType()->UniqueId();
				}
			}
			else
			{
				res = To Wstring(typeid(ug).name());

				if (Left(res, 5) == L"class")
					res = Right(res, res.size() - 6);
			}

			_RPT1(_CRT_WARN, "%S\n", res.c_str());
		}
*/
	}

	// Remove non-executing containers.
	auto i = modules.size() - 1;
	for (; i > 0; --i)
	{
		if (!modules[i]->GetFlag(UGF_NEVER_EXECUTES))
		{
			break;
		}

		modules[i]->moduleContainerIndex = -1;

		nonExecutingModules.push_back((ug_base*) modules[i]);
	}

	++i;
	if(i < modules.size())
		modules.erase(modules.begin() + i, modules.end());
}


int SeAudioMaster::getOptimumBlockSize()
{
#ifdef PROFILE_VARIOUS_BLOCK_SIZES
	return profileBlockSize;
#endif
#if 0 // def _DEBUG // trying to emulate macOS. blocksize 68, driver 1156 samples
	return 68;
#endif
	return OPTIMUM_BLOCK_SIZE;
}

SeAudioMaster::SeAudioMaster( float p_samplerate, ISeShellDsp* p_shell, ElatencyContraintType latencyConstraint) : EventProcessor()
	,next_master_clock(0)
	,m_sample_clock(0)
	,interrupt_flag( false )	// flag software interupt
	,interupt_start_fade_out(false)
	,maxLatency(0)
	,block_start_clock(0)
	,m_shell(p_shell)
	,synth_thread_running(false)
	,synth_thread_started(false)
	,temporaryHandle_(-20) // start at -20 and decrement for next one.
	,enableSleepOptimisation(true)
{
#ifdef _DEBUG
	events.read_only = true;
#endif

	setSampleRate( p_samplerate );

#if (defined(CANCELLATION_TEST_ENABLE) || defined(CANCELLATION_TEST_ENABLE2))
	getShell()->SetCancellationMode();
#endif

	// _RPT1(_CRT_WARN, "Samplerate %f\n", m_samplerate );

	// zero time structure
	memset(&m_vst_time_info, 0, sizeof(my_VstTimeInfo)); // note will zero mask too, so save it if nesc
	m_vst_time_info.tempo = 120.0;
	// application is not a DocOb, and has longer lifetime than Document. therefore can't have Unique ID as held by Document.
	RegisterDspMsgHandle( this, UniqueSnowflake::APPLICATION );

#if defined(LOG_PIN_EVENTS )
	eventLogFile.open("C:\\temp\\pinlog.txt");
#endif

	switch (latencyConstraint)
	{
	case ElatencyContraintType::Full:
		maxLatency = static_cast<int>(p_samplerate); // 1000ms
		break;

	case ElatencyContraintType::Constrained:
		maxLatency = static_cast<int>(0.05f * p_samplerate); // 5ms
		break;

	default:
		maxLatency = 0;
		break;
	}

#if defined( LOG_ALL_MODULES_CPU )
	if (EventProcessor::logFileCsv.is_open())
	{
		EventProcessor::logFileCsv.close();
	}
	EventProcessor::logFileCsv.open("c:/temp/cpu-log.csv");

	EventProcessor::logFileCsv << "time,cpu,handle,name\n";
#endif

	static const int guiFrameRate = 60;
	guiFrameRateSamples = (int)SampleRate() / guiFrameRate;

#ifdef _DEBUG
#if defined(__arm__)
    {
        _RPT0(_CRT_WARN, "SYNTHEDIT PROCESSOR: ARM\n" );
    }
#else
    {
        
        bool processIsTranslated = false;
#if defined(__APPLE__)
        {
            int ret = 0;
            size_t size = sizeof(ret);
            if (sysctlbyname("sysctl.proc_translated", &ret, &size, NULL, 0) != -1)
            {
                processIsTranslated = ret > 0;
            }
        }
#endif
        if(processIsTranslated)
        {
            _RPT0(_CRT_WARN, "SYNTHEDIT PROCESSOR: Intel (Rosetta)\n" );
        }
        else
        {
            _RPT0(_CRT_WARN, "SYNTHEDIT PROCESSOR: Intel\n" );
        }
    }
#endif
#endif
}

void SeAudioMaster::setMpeMode(int32_t mpemode)
{
	if(audioInModule)
		audioInModule->setMpeMode(mpemode);
}

AudioMasterBase::~AudioMasterBase()
{
#ifdef _DEBUG

	for( auto it = dbg_copy_output_array.begin(); it != dbg_copy_output_array.end() ; ++it )
	{
		delete *it;
	}

#endif

	delete main_container;
}

SeAudioMaster::~SeAudioMaster()
{
#if defined(_DEBUG)
	for (auto tableEntry : lookup_tables)
	{
		if (tableEntry->lookup_table->CheckOverwrite() == false)
		{
			wstring msg(L"WARNING: Shared Memory corrupted off end. Mem ID: ");
			msg += tableEntry->name;
			ugmessage(msg);
		}
	}
#endif
}

#if 0
void SeAudioMaster::BuildDspGraph(
	const char* structureXml,
	std::vector< std::pair<int32_t, std::string> >& pendingPresets,
	std::vector<int32_t>& mutedContainers
)
{
	TiXmlDocument doc;
	doc.Parse(structureXml);

	if (doc.Error())
	{
		std::wostringstream oss;
		oss << L"Module XML Error: [SynthEdit.exe]" << doc.ErrorDesc() << L"." << doc.Value();
		getShell()->Application()->SeMessageBox(oss.str().c_str(), L"", 0); // not mac: MB_OK | MB_ICONSTOP);

#if defined (_DEBUG)
		// Get error information and break.
		TiXmlNode* e = doc.FirstChildElement();
		while (e)
		{
			_RPT1(_CRT_WARN, "%s\n", e->Value());
			TiXmlElement* pElem = e->FirstChildElement("From");
			if (pElem)
			{
				[[maybe_unused]] const char* from = pElem->Value();
			}
			e = e->LastChild();
		}
		assert(false);
#endif

		return;
	}

	BuildDspGraph(&doc, pendingPresets, mutedContainers);
}
#endif

void SeAudioMaster::BuildDspGraph(
	TiXmlDocument* doc,
	std::vector< std::pair<int32_t, std::string> >& pendingPresets,
	std::vector<int32_t>& mutedContainers
)
{
	TiXmlHandle hDoc(doc);
	TiXmlElement* pElem;
	TiXmlElement* document_xml = nullptr;

	// block: name
	{
		document_xml = hDoc.FirstChildElement("Document").Element();
		pElem = document_xml->FirstChildElement("DSP");

		// should always have a valid root but handle gracefully if it does
		if (!pElem)
			return;
	}

	// block: DSP
	{
		// First element must be the Main Container.
		pElem = pElem->FirstChildElement();
		assert(strcmp(pElem->Value(), "Module") == 0);
		assert(strcmp(pElem->Attribute("Type"), "Container") == 0);
		auto moduleType = ModuleFactory()->GetById(L"Container");
		assert(main_container == 0);
		main_container = dynamic_cast<ug_container*>(moduleType->BuildSynthOb());
		main_container->Setup(this, pElem);

		main_container->SetupPatchManager(pElem->FirstChildElement("PatchManager"), pendingPresets);
		auto mc_patch_manager = main_container->get_patch_manager();

		if (main_container->isContainerPolyphonic())
		{
			int32_t VoiceCount = 6;
			int32_t VoiceReserveCount = 3;
			if (auto param = mc_patch_manager->GetHostControl(HC_POLYPHONY, main_container->Handle()); param)
			{
				VoiceCount = (int32_t)param->GetValueRaw2();
			}
			if (auto param = mc_patch_manager->GetHostControl(HC_POLYPHONY_VOICE_RESERVE, main_container->Handle()); param)
			{
				VoiceReserveCount = (int32_t)param->GetValueRaw2();
			}

			main_container->setVoiceReserveCount(VoiceReserveCount);
			main_container->setVoiceCount(VoiceCount);
		}

		// For polyphonic containers, help the patch manager (which may be in a higher container)
		// associate it's polyphonic parameter (gate etc) with the container* that is controlling the voices.
		mc_patch_manager->setupContainerHandles(main_container);

#if defined( _DEBUG )
		main_container->debug_name = L"Main";
#endif
		BuildModules(
			main_container,
			main_container,
			pElem,
			main_container,
			pendingPresets,
			mutedContainers
		);
		main_container->BuildAutomationModules();

#if defined( _DEBUG )
		if (!debug_missing_modules.empty())
		{
#if defined( _WIN32 )
			_RPT0(_CRT_WARN, "==== MISSING MODULES ====\n");
			for (auto& it : debug_missing_modules)
			{
				_RPT1(_CRT_WARN, "    %s\n", it.c_str());
			}
			_RPT0(_CRT_WARN, "==== MISSING MODULES ====\n");
			_RPT0(_CRT_WARN, "check that .cpp is included in project. Check INIT_STATIC_FILE(modulename) in UgDatabase.cpp\n");
#else
			std::cout << "==== MISSING MODULES ====\n";
			for (auto& it : debug_missing_modules)
			{
				std::cout << "    " << it.c_str() << "\n";
			}
			std::cout << "==== MISSING MODULES ====\n";
#endif
#ifdef _WIN32
			assert(false && "err: Module not yet included in project. (see 'debug_missing_modules' above)");
#endif
		}
#endif
	}

	if (!getShell()->isEditor())
	{
		// the loaded Container need input and output modules connected
		SetupVstIO();
	}

	main_container->PostBuildStuff(true);
	assert(dynamic_cast<ug_oversampler*>(main_container->AudioMaster()) == nullptr); // detect ultimate top-level container.
	main_container->PostBuildStuff_pass2();

	// debug poly flags
#ifdef _DEBUG
	if( GetDebugFlag( DBF_TRACE_POLY ) )
	{
		//todo		debugpoly();
	}
#endif
}

void SeAudioMaster::ApplyPinDefaultChanges(std::unordered_map<int64_t, std::string>& extraPinDefaultChanges)
{
	for (auto& p : extraPinDefaultChanges)
	{
		const auto handle = p.first >> 32;
		const auto pinIdx = static_cast<int32_t>(p.first & 0xFFFFFFFF);
		const auto& value = p.second;

		auto module = dynamic_cast<ug_base*>(m_handle_map[handle]);
		module->GetPlug(pinIdx)->SetDefault2(value.c_str());
	}
}

int32_t SeAudioMaster::RegisterIoModule(class ISpecialIoModule* m)
{
	if (auto audioout = dynamic_cast<ISpecialIoModuleAudioOut*>(m); audioout)
	{
		audioOutModule = audioout;
	}
	if (auto audioin = dynamic_cast<ISpecialIoModuleAudioIn*>(m); audioin)
	{
		audioInModule = audioin;
	}

	return getShell()->RegisterIoModule(m);
}

void SeAudioMaster::DoProcess_plugin(int sampleframes, const float* const* inputs, float* const* outputs, int numInputs, int numOutputs)
{
	DenormalFixer flushDenormals;

	if (audioOutModule)
	{
		audioOutModule->setIoBuffers(outputs, numOutputs);
	}
	else
	{
		// send silence.
		for (int chan = 0; chan < numOutputs; ++chan)
		{
			std::fill(outputs[chan], outputs[chan] + sampleframes, 0.0f);
		}
	}

	if (audioInModule)
	{
		audioInModule->setIoBuffers(inputs, numInputs);
	}	

	const auto sampleframesCopy = sampleframes;

	assert(m_sample_clock >= block_start_clock);

	if (interrupt_flag.load(std::memory_order_relaxed))
	{
		HandleInterrupt();
	}

	assert(events.empty()); // AudioMaster events not supported (use a proxy, like main container).

	// remember sampleclock at start of host's block
	host_block_start_clock = m_sample_clock;
	int block_size = AudioMaster()->BlockSize();
	timestamp_t next_bsc = BlockStartClock() + block_size;
	assert(m_sample_clock <= next_bsc);

	do
	{
		assert(m_sample_clock >= block_start_clock);
		int remain_in_block = (int)(next_bsc - m_sample_clock);

		if (remain_in_block == 0)
		{
#ifdef CANCELLATION_TEST_ENABLE2
			// only freeze once entire block filled.
			CancellationFreeze2(m_sample_clock);
#endif

			block_start_clock = next_bsc;
			next_bsc += block_size;
			remain_in_block = block_size;
		}

		int to_do = min(sampleframes, remain_in_block);
		assert(to_do >= 0 && to_do <= block_size);
		if (to_do > 0) // AU Validation requires plugin to handle zero sampleframes.
		{
			next_master_clock = m_sample_clock + to_do;
			assert(m_sample_clock >= block_start_clock);
			assert(next_master_clock > block_start_clock);
			//_RPT2(_CRT_WARN, "sub_process(to_do) this=%x sc=%d\n", this, m_sample_clock );
			processModules_plugin(
				getBlockPosition()
				, to_do
#ifdef _DEBUG
				, m_sample_clock
#endif
			);

			current_run_ug = 0;

#ifdef CANCELLATION_TEST_ENABLE
			CancellationFreeze(m_sample_clock);
#endif
			m_sample_clock += to_do;
			sampleframes -= to_do;
			assert(m_sample_clock >= block_start_clock);
			assert(m_sample_clock <= next_master_clock);
		}
	} while (sampleframes > 0);

	// Send updates to GUI
	m_shell->ServiceDspWaiters2(sampleframesCopy, guiFrameRateSamples);
}

void SeAudioMaster::DoProcess_editor(int sampleframes, const float* const* inputs, float* const* outputs, int numInputs, int numOutputs)
{
	DenormalFixer flushDenormals;

	if (audioOutModule)
	{
		audioOutModule->setIoBuffers(outputs, numOutputs);
	}

	if (audioInModule)
	{
		audioInModule->setIoBuffers(inputs, numInputs);
	}

	const auto sampleframesCopy = sampleframes;

	const auto cpuStartTime = std::chrono::steady_clock::now();

	// not possible to do random block sizes since DoProcessVST rolled into this function
#if 0 // defined(_DEBUG) && !(defined(CANCELLATION_TEST_ENABLE) || defined(CANCELLATION_TEST_ENABLE2))
	static bool debugReentrantFlag = false; // not in VST.

	if (!debugReentrantFlag && SeAudioMaster::GetDebugFlag(DBF_RANDOMISE_BLOCK_SIZE))
	{
		static int semi_random_size = -1; // not in VST.
		debugReentrantFlag = true;

		if (++semi_random_size >= sampleframes)
		{
			semi_random_size = 0;
		}

		int s2 = sampleframes - semi_random_size;
		assert(s2 > 0);
		DoProcessVST(semi_random_size);
		DoProcessVST(s2);
		debugReentrantFlag = false;

		const auto elapsed = std::chrono::steady_clock::now() - cpuStartTime;
		UpdateCpu(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
		return;
	}

#endif

	assert(m_sample_clock >= block_start_clock);

	if (interrupt_flag.load(std::memory_order_relaxed))
	{
		HandleInterrupt();
	}

//#if !defined( SE_ED IT_SUPPORT )
//	assert(events.empty()); // AudioMaster events not supported (use a proxy, like main container).
//#endif
// 
	// remember sampleclock at start of host's block
	host_block_start_clock = m_sample_clock;
	int block_size = AudioMaster()->BlockSize();
	timestamp_t next_bsc = BlockStartClock() + block_size;
	assert(m_sample_clock <= next_bsc);

	do
	{
		assert(m_sample_clock >= block_start_clock);
		int remain_in_block = (int)(next_bsc - m_sample_clock);

		if (remain_in_block == 0)
		{
#ifdef CANCELLATION_TEST_ENABLE2
			// only freeze once entire block filled.
			CancellationFreeze2(m_sample_clock);
#endif

			block_start_clock = next_bsc;
			next_bsc += block_size;
			remain_in_block = block_size;
		}

		// Handle IO Events.
		if (m_sample_clock == block_start_clock)
		{
			// events allowed only on host-block boundary
			assert(!events.empty());
			SynthEditEvent* next_event = events.front();
			assert(next_event->timeStamp >= m_sample_clock); // miss one?

			while (next_event->timeStamp == m_sample_clock)
			{
				events.pop_front();
				HandleEvent(next_event);
				delete_SynthEditEvent(next_event);
				assert(!events.empty());
				next_event = events.front();
			}
		}

		int to_do = min(sampleframes, remain_in_block);
		assert(to_do >= 0 && to_do <= block_size);
		if (to_do > 0) // AU Validation requires plugin to handle zero sampleframes.
		{
			next_master_clock = m_sample_clock + to_do;
			assert(m_sample_clock >= block_start_clock);
			assert(next_master_clock > block_start_clock);
			//_RPT2(_CRT_WARN, "sub_process(to_do) this=%x sc=%d\n", this, m_sample_clock );
			processModules_editor(
				getBlockPosition()
				, to_do
#ifdef _DEBUG
				, m_sample_clock
#endif
			);

			current_run_ug = 0;

#ifdef CANCELLATION_TEST_ENABLE
			CancellationFreeze(m_sample_clock);
#endif
			m_sample_clock += to_do;
			sampleframes -= to_do;
			assert(m_sample_clock >= block_start_clock);
			assert(m_sample_clock <= next_master_clock);
		}
	} while (sampleframes > 0);

	// Send updates to GUI
	m_shell->ServiceDspWaiters2(sampleframesCopy, guiFrameRateSamples);

	const auto elapsed = std::chrono::steady_clock::now() - cpuStartTime;
	UpdateCpu(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
}

void SeAudioMaster::MidiIn( int delta, const unsigned char* MidiMsg, int length )
{
	const timestamp_t clock = SampleClock() + delta;
	audioInModule->sendMidi(clock, { MidiMsg , length });
}

void SeAudioMaster::setParameterNormalizedDsp( int delta, int paramIndex, float value, int32_t flags )
{
	timestamp_t timestamp = Patchmanager_->Container()->CalculateOversampledTimestamp( main_container, SampleClock() + delta );

	Patchmanager_->setParameterNormalized( timestamp, paramIndex, value, flags );
}

void SeAudioMaster::setParameterNormalizedDaw(int delta, int32_t paramHandle, float value, int32_t flags)
{
	timestamp_t timestamp = Patchmanager_->Container()->CalculateOversampledTimestamp(main_container, SampleClock() + delta);

	Patchmanager_->setParameterNormalizedDaw(timestamp, paramHandle, value, flags);
}

void SeAudioMaster::setInputSilent(int input, bool isSilent)
{
	audioInModule->setInputSilent(input, isSilent);
}

uint64_t SeAudioMaster::getSilenceFlags(int output, int count)
{
	return audioOutModule->getSilenceFlags(output, count);
}

// the loaded Container need input and output modules connected
void SeAudioMaster::SetupVstIO()
{
	// Get first Container in project. This is the synth.
	Voice* v = main_container->front();

	ug_base* synthModule = nullptr;
	ug_container* synthContainer = nullptr;

	for( auto u : v->UGClones )
	{
		auto oversampler = dynamic_cast<ug_oversampler*>( u );
		if( oversampler )
		{
			synthContainer = oversampler->main_container;
			synthModule = u;
			break;
		}
		else
		{
			synthContainer = dynamic_cast<ug_container*>( u );
			if( synthContainer )
			{
				synthModule = u;
				break;
			}
		}
	}

	Patchmanager_ = synthContainer->get_patch_manager();

	Module_Info* moduleType = ModuleFactory()->GetById(L"VST Output");
	auto vst_out = moduleType->BuildSynthOb();
	audioOutModule = dynamic_cast<ISpecialIoModuleAudioOut*>(vst_out);
	main_container->AddUG(vst_out);
	vst_out->SetupWithoutCug( );//moduleType );
	moduleType = ModuleFactory()->GetById(L"VST Input");
	auto vst_in = moduleType->BuildSynthOb();
	audioInModule = dynamic_cast<ISpecialIoModuleAudioIn*>(vst_in);
	main_container->AddUG( vst_in );
	vst_in->SetupWithoutCug( );

	for( auto p : synthModule->plugs )
	{
		UPlug* connect_to = nullptr;

		if( p->DataType == DT_FSAMPLE )
		{
			ug_base* io_ug;
			EDirection io_direction;

			if( p->Direction == DR_OUT )
			{
				io_ug = vst_out;
				io_direction = DR_IN;
			}
			else
			{
				io_ug = vst_in;
				io_direction = DR_OUT;
			}

			connect_to = new UPlug(io_ug, io_direction, DT_FSAMPLE);
			io_ug->AddPlug( connect_to );
			connect_to->CreateBuffer();
		}

		if( p->DataType == DT_MIDI2 )
		{
			if( p->Direction == DR_OUT )
			{
				connect_to = vst_out->GetPlug(L"MIDI In");
			}
			else
			{
				connect_to = vst_in->GetPlug(L"MIDI Out");
			}
		}

		if( connect_to )
		{
			if( p->Direction == DR_OUT )
			{
				synthContainer->connect( p, connect_to );
			}
			else
			{
				// erase default connection.
				if (!p->connections.empty())
				{
					auto fromPin = p->connections.front();
					auto it = std::find(fromPin->connections.begin(), fromPin->connections.end(), p);
					fromPin->connections.erase(it);

					p->connections.clear();
				}

				synthContainer->connect( connect_to, p );
			}
		}
	}
}

// We don't bother oversampling if host already running in HD, likewise if already in nested oversampler.
int AudioMasterBase::calcOversampleFactor( int oversampleFactor, int sampleRate, bool hdMode )
{
	int referenceSampleRate = 50000; // maximum 1x samplerate

	if( !hdMode )
	{
		referenceSampleRate /= 2;
	}

	int maximumSr = referenceSampleRate * oversampleFactor;

	oversampleFactor = ( std::max )( oversampleFactor, 1 );

	// Scale back oversampling at HD audio rates ( anything over 48kHz ).
	while( sampleRate * oversampleFactor > maximumSr && oversampleFactor > 1 )
	{
		oversampleFactor = oversampleFactor >> 1;
	}

	if( oversampleFactor < 2 ) // 0 = off.
	{
		oversampleFactor = 0;
	}

	return oversampleFactor;
	//                            _RPTW3(_CRT_WARN, L"HD=%d HOST-SR=%f SE-SR=%f\n", m_waves_hd_mode, m_samplerate, m_samplerate * (std::max)(1,oversampleFactor) );
}

void AudioMasterBase::BuildModules(
	ug_container* container,
	ug_container* patch_control_container,
	TiXmlElement* xml,
	ug_base* cpu_parent,
	std::vector< std::pair<int32_t, std::string> >& pendingPresets,
	std::vector<int32_t>& mutedContainers
)
{
	// Look for "Modules" list.
	TiXmlElement* modules_xml = xml->FirstChildElement("Modules");

	if( modules_xml )
	{
		for( TiXmlElement* pElem = modules_xml->FirstChildElement("Module"); pElem; pElem=pElem->NextSiblingElement("Module"))
		{
			const char* typeS = pElem->Attribute("Type");
			auto moduleType = ModuleFactory()->GetById(Utf8ToWstring(typeS));

			ug_base* generator = nullptr;
            if(moduleType)
            {
				generator = moduleType->BuildSynthOb();
            }

			if(!generator)
			{
#if defined( _DEBUG)
				auto& missing = container->AudioMaster2()->debug_missing_modules;

                if(find(missing.begin(), missing.end(), typeS) == missing.end())
					missing.push_back(typeS);
#endif
				continue;
			}

			// when expanding inline, a module can have a different patch manager than other in the same container
			// we use the call-stack to bookkeep this
			generator->patch_control_container = patch_control_container;

			// this is "build-in" container, not correct parent container.
			generator->cpuParent = cpu_parent; // container;
			const bool isContainer = strcmp(typeS, "Container") == 0;

			if( isContainer )
			{
				auto child_container = dynamic_cast<ug_container*>(generator);
				auto child_patch_control_container = patch_control_container;

				// child not deserialized yet, so get it's handle manually.
				int32_t child_handle{};
				pElem->QueryIntAttribute("Id", &child_handle);
				child_container->SetHandle(child_handle);

				// we don't nesc have the chain of parent containers setup, so manually figure out where the patch manager is
				auto child_patch_manager = container->get_patch_manager(); // default to parent.

				// Build PatchManager if needed.
				TiXmlElement* child_patchManager_xml{};
				{
					child_patchManager_xml = pElem->FirstChildElement("PatchManager");
					if (child_patchManager_xml)
					{
						child_container->SetupPatchManager(child_patchManager_xml, pendingPresets);
						child_patch_manager = child_container->get_patch_manager();
						child_patch_control_container = child_container;
					}
				}

				assert((child_container == child_patch_control_container) == (child_patchManager_xml != nullptr));
				
				
#if SE_RUNTIME_MUTABLE_CONTAINERS
				{
					int handle = -1;
					pElem->QueryIntAttribute("Id", &handle);

					if (std::find(mutedContainers.begin(), mutedContainers.end(), handle) != mutedContainers.end())
					{
						delete generator;
						continue;
					}
				}
#endif
				int expandInline = 1;			// default is Yes.
				pElem->QueryIntAttribute("ExpandInline", &expandInline);

				int oversampleFactor = 0;
				{
					auto oversamplingParam = child_patch_manager->GetHostControl(HC_OVERSAMPLING_RATE, child_handle);
					if (oversamplingParam)
					{
						oversampleFactor = (int32_t)oversamplingParam->GetValueRaw2();
					}
				}
				
				if (oversampleFactor > 0 && SampleRate() == container->getSampleRate()) // scale back oversampling if host already in HD (unless nested).
				{
					oversampleFactor = calcOversampleFactor( oversampleFactor, (int) container->getSampleRate() );
				}

				// With Oversampling we substitute ug_oversampler, then put the container inside the oversampler.
				if( oversampleFactor > 0 )
				{
					expandInline = 0;
				}
				else
				{
					// With no Oversampling we just add the container alongside it's modules.
					container->AddUG( generator );
				}

				// attempt to fix default values ignored by oversampler (becuase it couldn't get to the defaut setter)
				auto original_parent = generator->parent_container;
				if(!original_parent)
					generator->parent_container = container;

				generator->Setup( /*this*/ container->AudioMaster(), pElem);
					
				generator->parent_container = original_parent;

				if (child_container->isContainerPolyphonic())
				{
					int32_t VoiceCount = 6;
					int32_t VoiceReserveCount = 3;
					if (auto param = child_patch_manager->GetHostControl(HC_POLYPHONY, child_handle); param)
					{
						VoiceCount = (int32_t)param->GetValueRaw2();
					}
					if (auto param = child_patch_manager->GetHostControl(HC_POLYPHONY_VOICE_RESERVE, child_handle); param)
					{
						VoiceReserveCount = (int32_t)param->GetValueRaw2();
					}

					child_container->setVoiceReserveCount(VoiceReserveCount);
					child_container->setVoiceCount(VoiceCount);
				}

				if(	expandInline )
				{
					generator->SetFlag(UGF_NEVER_EXECUTES);
					BuildModules( container, child_patch_control_container, pElem, generator, pendingPresets, mutedContainers);
				}
				else // Build self-contained container.
				{
					// Oversampling.
					ug_oversampler* oversampler = 0;
					if( oversampleFactor != 0 )
					{
						int oversamplerFilterPoles_ = 7; // default.
						{
							auto oversamplingParam = child_patch_manager->GetHostControl(HC_OVERSAMPLING_FILTER, child_handle);
							if (oversamplingParam)
							{
								oversamplerFilterPoles_ = (int32_t)oversamplingParam->GetValueRaw2();
							}
						}
#if 0
// In plugins this is redundant, the patch-parameter value will already be available and overwrite this.
// Retained only for Waves and SE at present. Could probaby remove it from SE also if SE supported getPersisentHostControl()
pElem->QueryIntAttribute("OversampleFilter", &oversamplerFilterPoles_);
						{
							int handle;
							pElem->QueryIntAttribute("Id", &handle);
							oversamplerFilterPoles_ = (int)getShell()->getPersisentHostControl(handle, HC_OVERSAMPLING_FILTER, RawView(oversamplerFilterPoles_));
						}
#endif

//	?? if not already child_container->patch_control_container = container->patch_control_container;

						oversampler = new ug_oversampler();
						AssignTemporaryHandle(oversampler);
						container->AddUG( oversampler );
//							oversampler->SetAudioMaster(this);
						oversampler->SetAudioMaster(container->AudioMaster());
						oversampler->main_container = child_container;
						oversampler->Setup1( oversampleFactor, oversamplerFilterPoles_ ); // reassigns childs audiomaster to itself
						oversampler->cpuParent = container;

						// Containers without own patch manager need PM proxy.
						if( !pElem->FirstChildElement("PatchManager"))
						{
							oversampler->CreatePatchManagerProxy();
						}
					}

					// For polyphonic containers, help the patch manager (which may be in a higher container)
					// associate it's polyphonic parameter (gate etc) with the container* that is controlling the voices.
					child_container->get_patch_manager()->setupContainerHandles(child_container);

					// possible should be oversampler->BuildModules() because oversampler is the "AudioMaster"  LOOK DOWN to generator->Setup(this, pElem);

					// re-route from container to oversampler in/out modules.
					if( oversampleFactor )
					{
						oversampler->BuildModules( child_container, child_patch_control_container, pElem, oversampler, pendingPresets, mutedContainers);
						oversampler->Setup2(true);
					}
					else
					{
						BuildModules(child_container, child_patch_control_container, pElem, child_container, pendingPresets, mutedContainers);
						child_container->PostBuildStuff(true);
					}

					if (child_container == child_patch_control_container)
					{
						child_container->BuildAutomationModules();
					}
				}
			}
			else
			{
				// Regular module.
				container->AddUG(generator);
				generator->Setup(this, pElem); // "this" is meant to be audiomaster of generator, but in case of oversampling, it's not the correct one.
			}
		}
	}

	// Lines.
	TiXmlElement* linesElement = xml->FirstChildElement("Lines");

	if( linesElement )
	{
		for( TiXmlElement* lineElement = linesElement->FirstChildElement("Line"); lineElement; lineElement=lineElement->NextSiblingElement("Line"))
		{
			// or quicker to iterate?
			int fromModule = -1;
			int toModule = -1;
			int fromPinIndex = 0; // default if not specified.
			int toPinIndex = 0;
			lineElement->QueryIntAttribute("From", &fromModule);
			lineElement->QueryIntAttribute("To", &toModule);
			lineElement->QueryIntAttribute("FromPin", &fromPinIndex);
			lineElement->QueryIntAttribute("ToPin", &toPinIndex);
			ug_base* from = dynamic_cast<ug_base*>( HandleToObject( fromModule ) );
			ug_base* to = dynamic_cast<ug_base*>( HandleToObject( toModule ) );

			if( from && to ) // are not muted.
			{
				from->connect( from->GetPlug(fromPinIndex), to->GetPlug(toPinIndex) );
			}
		}
	}
	
}

void SeAudioMaster::end_run()
{ 
	if (getShell()->isEditor())
	{
		if (audioOutModule)
		{
			TriggerShutdown();
		}
		else
		{
			// cause audio driver to stop ASAP (important for unit tests to record correct length of wavefile).
			state = audioMasterState::Stopping;
			getShell()->OnFadeOutComplete();
		}
	}
}

void SeAudioMaster::UpdateCpu(int64_t nanosecondsElapsed)
{
	cpuConsumption[cpuConsumptionIndex] = static_cast<uint16_t>(nanosecondsElapsed);

	if (++cpuConsumptionIndex == CPU_BATCH_SIZE)
	{
		cpuConsumptionIndex = 0;

		auto queue = getShell()->MessageQueToGui();

		if (!my_msg_que_output_stream::hasSpaceForMessage(queue, sizeof(cpuConsumption) + sizeof(int32_t) ))
			return; // no space in queue.

		my_msg_que_output_stream strm(queue, Handle(), "cput"); // total CPU.
		strm << static_cast<int32_t>(sizeof(cpuConsumption)); // message length.

		strm.Write(cpuConsumption, sizeof(cpuConsumption));
		strm.Send();

		// hover-scope
		if (hoverScopePin)
		{
			if (DT_FSAMPLE == hoverScopePin->DataType)
			{
				/*
				const float currentValue = hoverScopePin->GetSamplePtr()[0];

				my_msg_que_output_stream strm(queue, hoverScopePin->UG->Handle(), "hvsd");
				strm << static_cast<int32_t>(sizeof(float)); // message length.
				strm << currentValue;
				strm.Send();
				*/

			}
			else
			{
				UPlug* ValuePin{};
				if (hoverScopePin->Direction == DR_IN)
				{
					ValuePin = hoverScopePin->connections.empty() ? nullptr : hoverScopePin->connections[0];
				}
				else
				{
					ValuePin = hoverScopePin;
				}

				if (ValuePin)
				{
					RawView rview(ValuePin->currentRawValue);

					if (DT_FLOAT == hoverScopePin->DataType)
					{
						const auto currentValue = (float)rview;

						my_msg_que_output_stream strm(queue, hoverScopePin->UG->Handle(), "hvsd");
						strm << static_cast<int32_t>(sizeof(float)); // message length.
						strm << currentValue;
						strm.Send();
					}
				}
			}
		}
	}
}

void SeAudioMaster::HandleEvent(SynthEditEvent* e)
{
	//	assert( !sleeping );
	assert(e->timeStamp == SampleClock());

	switch (e->eventType)
	{

	case UET_RUN_FUNCTION2:
	{
		ug_func func[2] = { nullptr, nullptr }; // prevent stack overwrite on 32-bit systems.
		// only copies some of function pointer, assumes remainder is zeroed.
		int32_t* i = (int32_t*)&func;
		i[0] = e->parm2;
		i[1] = e->parm3;
		(this->*(func[0]))();
	}
	break;

	default:
		assert(false); // un-handled event
	};
}

// perform an async restart on a worker thread.
void SeAudioMaster::TriggerRestart()
{
	// may come from UI thread.
	if (state != audioMasterState::Running)
		return;

	state = audioMasterState::AsyncRestart;
//	_RPT0(0, "audioMasterState::AsyncRestart\n");

	interupt_start_fade_out = true;
	TriggerInterrupt();
}

void SeAudioMaster::ClearDelaysUnsafe()
{
	interrupt_clear_delays = true;
	TriggerInterrupt();
}

void SeAudioMaster::TriggerShutdown()
{
	if (state == audioMasterState::Stopped)
		return;

	// may come from UI thread.
	state = audioMasterState::Stopping;
//	_RPT0(0, "audioMasterState::Stopping\n");

	interupt_start_fade_out = true;
	TriggerInterrupt();
}

void SeAudioMaster::HandleInterrupt()
{
	interrupt_flag.store(false, std::memory_order_release);

	//		_RPT1(_CRT_WARN, "SeAudioMaster::Do Process() interrupt %d\n", SampleClock() );

	if (auto preset = interrupt_preset_.exchange(nullptr, std::memory_order_relaxed); preset)
	{
		Patchmanager_->setPreset(preset);
	}

	if( interupt_start_fade_out )
	{
		interupt_start_fade_out = false;

		if (audioOutModule) // will be null under automated testing.
		{
			audioOutModule->startFade(true); // TODO: fade up all notes off (ref SE1.4 'SetDucked')
		}
		else // stop immediatly
		{
			onFadeOutComplete();
		}
	}

	if(interupt_module_latency_change)
	{
		interupt_module_latency_change = false;

		bool latencyNeedsCalculating = false;
		const auto& moduleLatencies = getShell()->GetModuleLatencies();
		for(auto& l : moduleLatencies)
		{
			auto module = dynamic_cast<ug_base*>(AudioMaster()->HandleToObject(l.first));
			if(module->latencySamples != l.second)
			{
				latencyNeedsCalculating = true;
				break;
			}
		}

		// if any changed, do an async restart
		if(latencyNeedsCalculating)
		{
			m_shell->DoAsyncRestart();
		}
	}

	if (interrupt_clear_delays)
	{
		interrupt_clear_delays = false;
		if (SampleClock() != 0) // no need to clear tails on a complete reset.
		{
			_RPT0(0, "interrupt_clear_delays\n");
			/* not needed at present
			#ifndef SE_EDIT_SUPPORT
						vst_out->MuteUntilTailReset();
			#endif
			*/
			SetHostControl(HC_CLEAR_TAILS, hCClearTailsNextValue++);
		}
		else
		{
			_RPT0(0, "interrupt_clear_delays - ignored (sampleclock == 0)\n");
		}
	}
}

void SeAudioMaster::SetModuleLatency(int32_t handle, int32_t latency)
{
	auto& moduleLatencies = getShell()->GetModuleLatencies();

	auto it = moduleLatencies.find(handle);

	if (it == moduleLatencies.end())
	{
		if (latency == 0) // avoid too many zero entries, they don't achieve anything.
		{
			return;
		}

		moduleLatencies.insert({ handle, latency });
	}
	else
	{
		if ((*it).second == latency)
		{
			return;
		}

		(*it).second = latency;
	}

	interupt_module_latency_change = true;
	TriggerInterrupt();
}

void SeAudioMaster::onFadeOutComplete()
{
	getShell()->OnFadeOutComplete();
}

bool SeAudioMaster::onQueMessageReady( int handle, int msg_id, my_input_stream& p_stream )
{
	//_RPTW1(_CRT_WARN, L"OnDspMsg: id = %s\n", long_to_id(msg_id) );
	dsp_msg_target* target = HandleToObject(handle);
	if( target ) 
	{
		target->OnUiMsg( msg_id, p_stream );
		return true;
	}

	return false;// robust handling of muted modules sending messages.
}

void SeAudioMaster::OnUiMsg(int p_msg_id, my_input_stream& p_stream)
{
	if (p_msg_id == id_to_long2("EIPC")) // Emulate Ignore Program Change
	{
		Patchmanager_->OnUiMsg(p_msg_id, p_stream);
		getShell()->EnableIgnoreProgramChange();
		return;
	}
	if (p_msg_id == id_to_long2("hvsc")) // hover-scope
	{
		int32_t moduleHandle{};
		int32_t pinIdx{};
		p_stream >> moduleHandle;
		p_stream >> pinIdx;
		_RPTN(0, "hover-scope: %d %d\n", moduleHandle, pinIdx);

		hoverScopePin = {};
		hoverScopeModule = {};
		if (pinIdx > -1) // -1 = none
		{
			if (auto hoverModule = dynamic_cast<ug_base*>(HandleToObject(moduleHandle)); hoverModule)
			{
				assert(hoverModule->plugs.size() > pinIdx && pinIdx >= 0);
				hoverScopePin = hoverModule->GetPlug(pinIdx);

				if (hoverScopePin)
				{
					if (hoverScopePin->DataType == DT_FSAMPLE)
					{
						hoverScopeModule = std::make_unique<HoverScopeAudioCollector>(
							hoverScopePin->UG->Handle()
							, SampleRate()
							, hoverScopePin->GetSamplePtr()
							, getShell()->MessageQueToGui()
						);
					}
				}
			}
		}
	}

	dsp_msg_target::OnUiMsg( p_msg_id, p_stream );
}

int SeAudioMaster::latencyCompensationMax()
{
	return maxLatency;
}

void SeAudioMaster::ServiceGuiQue()
{
	// GUI->DSP
	//moved m_message_que_ui_to_dsp.pollMessage(this);
	// DSP->GUI
	getShell()->ServiceDspRingBuffers();
}

void AudioMasterBase::CancellationFreeze([[maybe_unused]] timestamp_t sample_clock)
{
#ifdef CANCELLATION_TEST_ENABLE
	static bool done = false;
	if (sample_clock >= CANCELLATION_SNAPSHOT_TIMESTAMP && !done)
	{
		done = true;
		auto blockSize = BlockSize();
		string outputFolder;
#ifdef _WIN32
		outputFolder = "C:";
#endif
		outputFolder += "/temp/cancellation/" CANCELLATION_BRANCH;

		CreateFolderRecursive(Utf8ToWstring(outputFolder));

		for (auto it = activeModules.begin(); it != activeModules.end(); ++it)
		{
			auto ug = dynamic_cast<ug_base*>(*it);
			int idx = 0;
			bool printedModuleXml = false;
			for (auto plg : ug->plugs)
			{
				if (plg->DataType == DT_FSAMPLE)
				{
					if (plg->Direction == DR_OUT)
					{
						auto block_ptr = plg->GetSampleBlock();
						auto o = block_ptr->GetBlock(); // +lBlockPosition;
/*
						if (!printedModuleXml)
						{
							printedModuleXml = true;
							_RPTW2(_CRT_WARN, L"<Module name=\"%s\" handle=\"%d\">\n", ug->DebugModuleName().c_str(), ug->Handle());
						}
						_RPTW2(_CRT_WARN, L"  <Pin idx=\"%d\" value=\"%f\" />\n", idx, (float)*o);

*/
// set filename based on target handle and plug.
						{
							std::ostringstream oss;
							oss << outputFolder <<
								"/m_" << ug->Handle()
								<< "_" << ug->GetSortOrder()
								<< "_" << plg->getPlugIndex()
								<< ".raw";

							auto file = fopen(oss.str().c_str(), "wb");

							fwrite(o, sizeof(float), blockSize, file);

							fclose(file);
						}
					}
				}
				++idx;
			}
			if (printedModuleXml)
			{
				_RPTW0(_CRT_WARN, L"</Module>\n");
			}
		}
	}
#endif
}
 
#ifdef CANCELLATION_TEST_ENABLE2
void AudioMasterBase::CancellationFreeze2(timestamp_t sample_clock)
{
	if (sample_clock >= CANCELLATION_SNAPSHOT_TIMESTAMP && !cancellation_done)
	{
		cancellation_done = true;

		const int32_t blockSize = BlockSize();
		string outputFolder;
#ifdef _WIN32
		outputFolder = "C:";
#else
        const auto documents = BundleInfo::instance()->getUserDocumentFolder();
		outputFolder = WStringToUtf8(documents);
#endif
		outputFolder += "/temp/cancellation/" CANCELLATION_BRANCH;

		CreateFolderRecursive(Utf8ToWstring(outputFolder));

		FILE* file = {};
		{
			std::ostringstream oss;
			oss << outputFolder << "/snapshot.raw";
			file = fopen(oss.str().c_str(), "wb");
		}
  
        if(!file)
            return;

		// Write blocksize
		fwrite(&blockSize, sizeof(blockSize), 1, file);

		WriteCancellationData(blockSize, file);

		fclose(file);
	}
}

void AudioMasterBase::WriteCancellationData(int32_t blockSize, FILE* file)
{
	for (auto it = activeModules.inclusiveBegin(); it != activeModules.inclusiveEnd(); ++it)
	{
		auto ug = dynamic_cast<ug_base*>(*it);

		// skip containers that have been replaced with oversamplers (becuase they have same handle = confuses analysis)
		if (auto cont = dynamic_cast<ug_container*>(ug); cont)
		{
			if (dynamic_cast<ug_oversampler*>(ug->AudioMaster()))
			{
				continue;
			}
		}

		const int32_t pinCount = static_cast<int32_t>(std::count_if(
			ug->plugs.begin(),
			ug->plugs.end(),
			[](UPlug* plg) {return plg->DataType == DT_FSAMPLE && plg->Direction == DR_OUT; }
			));

		if (pinCount)
		{
			// Write module identity (address)
			fwrite(&ug, sizeof(ug), 1, file);

			// Write module handle
			const int32_t handle = ug->Handle();
			fwrite(&handle, sizeof(handle), 1, file);
            assert(handle != 0 && handle != 0xcdcdcdcd);

			// Write module voice
			fwrite(&ug->pp_voice_num, sizeof(ug->pp_voice_num), 1, file);

			// Write pin count
			fwrite(&pinCount, sizeof(pinCount), 1, file);

			for (auto plg : ug->plugs)
			{
				if (plg->DataType == DT_FSAMPLE && plg->Direction == DR_OUT)
				{
					const int32_t connectionsCount = static_cast<int32_t>(plg->connections.size());
					// Write connections count
					fwrite(&connectionsCount, sizeof(connectionsCount), 1, file);

					if (connectionsCount) // save space on un-connected pins.
					{
						// write destination modules.
						for (auto c : plg->connections)
						{
							fwrite(&c->UG, sizeof(c->UG), 1, file);
						}

						auto o = plg->GetSamplePtr();
						// write samples to file
						fwrite(o, sizeof(*o), blockSize, file);
					}
				}
			}
		}

		auto oversampler = dynamic_cast<ug_oversampler*>(ug);
		if (oversampler)
		{
			oversampler->WriteCancellationData(blockSize, file);
		}
	}
}

#endif

void AudioMasterBase::processModules_plugin(
	int lBlockPosition
	, int sampleframes
#ifdef _DEBUG
	, timestamp_t sample_clock
#endif
	)
{
#ifdef _DEBUG
	const bool check_blocks = GetDebugFlag(DBF_BLOCK_CHECK);
#endif

	const auto moduleEndIt = activeModules.end();
	for (auto it = activeModules.begin(); it != moduleEndIt; ++it)
	{
		auto ug = *it;
		current_run_ug = ug;

#if defined( _DEBUG )
		// below can happen when ug was woken AFTER it should have executed Wake()
		// e.g. by patch change downstream.
		assert( !ug->sleeping );
		assert( (ug->flags & UGF_SUSPENDED) == 0 );
		assert( ug->SampleClock() == sample_clock );
		assert(ug->AudioMaster() == this); // to ensure blockpos correct

		if(check_blocks)
		{
			copy_buffers(dynamic_cast<ug_base*>(ug));
		}
#endif

#if defined(LOG_PIN_EVENTS )
		ug->LogEvents(eventLogFile);
#endif

		ug->DoProcess( lBlockPosition, sampleframes );

		assert(ug->sleeping || ug->SampleClock() == sample_clock + sampleframes);
	}
}

void AudioMasterBase::processModules_editor(
	int lBlockPosition
	, int sampleframes
#ifdef _DEBUG
	, timestamp_t sample_clock
#endif
)
{
#ifdef _DEBUG
	const bool check_blocks = GetDebugFlag(DBF_BLOCK_CHECK);
#endif

	const auto moduleEndIt = activeModules.end();
	for (auto it = activeModules.begin(); it != moduleEndIt; ++it)
	{
		auto ug = *it;
		current_run_ug = ug;

#if defined( _DEBUG )
		// below can happen when ug was woken AFTER it should have executed Wake()
		// e.g. by patch change downstream.
		assert(!ug->sleeping);
		assert((ug->flags & UGF_SUSPENDED) == 0);
		assert(ug->SampleClock() == sample_clock);
		assert(ug->AudioMaster() == this); // to ensure blockpos correct

		if (check_blocks)
		{
			copy_buffers(dynamic_cast<ug_base*>(ug));
		}
#endif

#if defined(LOG_PIN_EVENTS )
		ug->LogEvents(eventLogFile);
#endif

		// CPU counting.
		const auto cpuStartTime = std::chrono::steady_clock::now();

		if (ug->m_debugger)
		{
			ug->m_debugger->Process(lBlockPosition, sampleframes);
		}
		else
		{
			ug->DoProcess(lBlockPosition, sampleframes);
		}

		// CPU counting.
		const auto elapsed = std::chrono::steady_clock::now() - cpuStartTime;
		ug->UpdateCpu(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());

#if defined (_DEBUG) 
		checkBuffers(ug, sample_clock, getBlockPosition(), sampleframes, check_blocks);
#endif
		assert(ug->sleeping || ug->SampleClock() == sample_clock + sampleframes);
	}

	if (hoverScopeModule)
		hoverScopeModule->process(lBlockPosition, sampleframes);
}

#if defined( _DEBUG )
void AudioMasterBase::checkBuffers(EventProcessor* ug, timestamp_t sample_clock, int debug_start_pos, int sampleframes, bool check_blocks)
{
	// check stat sent on all output pins.
	if (sample_clock == 0)
	{
		auto u = dynamic_cast<ug_base*>(ug);  //hacky

		for (auto plg : u->plugs)
		{
			if (plg->Direction == DR_OUT && plg->DataType != DT_MIDI2)
			{
				// ignore container/io_mod plugs that r re-routed.
				if ( dynamic_cast<ug_voice_splitter*>(ug) == 0 && ((dynamic_cast<ug_container*>(ug) == 0 && dynamic_cast<ug_io_mod*>(ug) == 0) || plg->InUse()))
				{
					if (dynamic_cast<ug_oversampler_in*>(ug) == 0)
					{
						assert(plg->debug_sent_status && "ug didn't send state change on all outputs at startup");
					}
				}
			}
		}
	}

	if (check_blocks)
	{
		ug_base* u = dynamic_cast<ug_base*>(ug);  //hacky
		verify_buffers(u, debug_start_pos, sampleframes);
	}

	bool check_values = GetDebugFlag(DBF_OUT_VAL_CHECK);
	if (check_values)
	{
		ug_base* u = dynamic_cast<ug_base*>(ug);  //hacky
		check_out_values(u, debug_start_pos, sampleframes);
	}
}
#endif

// display a message in user interface thread
void SeAudioMaster::ugmessage(std::wstring msg, int type) // defaults to MB_OK (zero)
{
	_RPTW1(_CRT_WARN, L"%s\n", msg.c_str() );

	my_msg_que_output_stream strm( getShell()->MessageQueToGui(), Handle(), "mbox");
	strm << (int) ( sizeof(wchar_t) * msg.size() + sizeof(int) + sizeof(type)); // message length.
	strm << msg;
	strm << type;
	strm.Send();
}

std::wstring SeAudioMaster::GetMidiPath()
{
	return getShell()->getDefaultPath(L"mid");
}

std::wstring SeAudioMaster::GetAudioPath()
{
	return getShell()->getDefaultPath(L"wav");
}

/*
For high-performance games, the solution is either to use the multimedia timer in a 16-bit DLL or to turn to the highest-resolution time service of all, QueryPerformanceCounter. This function was designed mainly for profiling, but there's no reason we can't use it as a general-purpose clock.

Like timeGetTime, QueryPerformanceCounter returns the time elapsed since the system started. The unit of measurement is determined by the hardware; on Intel-based CPUs it is about 0.8 microseconds.

The following fragment of a WinMain function shows how you might use QueryPerformanceCounter to update your game world every tenth of a second.

#define UPDATE_TICKS_MS 100    // milliseconds per world update

_int64 start, end, freq, update_ticks_pc
MSG     msg;

// Get ticks-per-second of the performance counter.
//   Note the necessary typecast to a LARGE_INTEGER structure
if (!QueryPerformanceFrequency((LARGE_INTEGER*)&freq))
  return -1;  // error  hardware doesn't support performance counter

// Convert milliseconds per move to performance counter units per move.
update_ticks_pc = UPDATE_TICKS_MS * freq / 1000;

// Initialize the counter.
QueryPerformanceCounter((LARGE_INTEGER*)&start);
*/

#ifdef _DEBUG
// ug want to be on blockdev list
void SeAudioMaster::Wake(EventProcessor* ug)
{
	assert( (ug->flags & UGF_SUSPENDED) == 0 );

	if( ug->sleeping ) // may not be
	{
		InsertBlockDev( ug );
	}
}

bool AudioMasterBase::GetDebugFlag(int flag)
{
	// DBF_BLOCK_CHECK will mess up SvFilter3 stability check. DUBUG_SORT
	// DBF_RANDOMISE_BLOCK_SIZE affects cancellation test (unless you restart between every render)
	const int debug_flags = 0; // DBF_RANDOMISE_BLOCK_SIZE;// | DBF_TRACE_POLY;// | DBF_BLOCK_CHECK;
	return (debug_flags & flag) != 0;
}
#endif

void SeAudioMaster::InsertBlockDev( EventProcessor* p_module )
{
	//	_RPT2(_CRT_WARN, "inserting ug (%x) into activeModules (sort%d)\n", p_module, p_module->GetSortOrder() );
#ifdef _DEBUG
	bool is_container = dynamic_cast<ug_base*>( p_module ) != 0;
	assert( p_module->SortOrder >= 0 || is_container); // sort order set??
#endif

	activeModules.insertSorted(p_module);
	bool missedExecution = current_run_ug && current_run_ug->moduleContainerIndex > p_module->moduleContainerIndex;

	// if prev in list has already executed, new module will miss out till next block.
	if(missedExecution)
	{
		p_module->SetClockRescheduleEvents( NextGlobalStartClock() );
	}
	else
	{
		p_module->SetClockRescheduleEvents( SampleClock() );
	}
}

// ug want to removed from blockdev list
void SeAudioMaster::SuspendModule( ug_base* p_module )
{
	//	_RPT2(_CRT_WARN, "suspend ug (%x) from activeModules (sort%d)\n", p_module, p_module->GetSortOrder() );
	activeModules.erase( p_module );
}

#ifdef _DEBUG
void AudioMasterBase::CheckForOutputOverwrite()
{
	for (auto it = activeModules.begin(); it != activeModules.end(); ++it)
	{
		(*it)->CheckForOutputOverwrite();
	}
}

bool CheckOverwrite(const float* m_samples, int debugBlocksize_, bool simdMightOverwrite)
{
	constexpr int simdSize = 4;

	// buffers are allocated to the next SIMD boundary.
	const int paddedBufferSize = (debugBlocksize_ + simdSize - 1) & ~(simdSize - 1);

	// some modules ask for permission to (harmlessly) write over the extra bytes.
	const int writableSize = simdMightOverwrite ? paddedBufferSize : debugBlocksize_;

#ifdef _DEBUG
	// in debug mode, we allocate and extra 4 bytes to check for misbehaving modules.
	constexpr int overwriteCheckSize = simdSize;
	const int totalBufferSize = paddedBufferSize + overwriteCheckSize;
#else
	const int totalBufferSize = paddedBufferSize;
#endif

	for(int i = writableSize ; i < totalBufferSize ; ++i)
	{
		if(m_samples[i] != magic_guard_number)
		{
			return true;
		}
	}

	return false;
#if 0

	const float* past_end = m_samples + debugBlocksize_;


#ifdef _DEBUG
//	const auto custom_guard_number = magic_guard_number + (((intptr_t)m_samples >> 10) & 0xff);
	const auto simdFriendlyBufferSize = overwriteCheckSize + (debugBlocksize_ + simdSize - 1) & ~(simdSize - 1);
	const auto safetyZoneSize = simdMightOverwrite ? overwriteCheckSize : simdFriendlyBufferSize - debugBlocksize_;

	const float* past_end = m_samples + debugBlocksize_;

	for (int i = /*USAMPLEBLOCK_SAFETY_ZONE_SIZE*/safetyZoneSize - 1; i >= 0; --i)
	{
		if (past_end[i] != magic_guard_number)
		{
			return i + 1;
		}
	}
#endif

	return 0;
#endif
}

// debug routines to check that ug only writes data to correct portion of output block
void AudioMasterBase::copy_buffers(ug_base* ug)
{
	const auto blockSize = BlockSize();
	bool block_ok = true;
	std::wstring msg;
	//	assert( ug->GetNumOutputs() < 40 ); // max supported ( in constructor )
	//GetNumOutputs don't work on adder
	//		for( int o = 0 ; o < ug->GetNumOutputs() ; o ++ )
	int blocks = (int) ug->plugs.size();

	if( blocks > MAX_DEBUG_BUFFERS )
		blocks = MAX_DEBUG_BUFFERS;

	for( int p = 0 ; p < blocks ; p++ )
	{
		UPlug* plg = ug->plugs[p];
		if( plg->DataType == DT_FSAMPLE )
		{
			const auto* block_ptr = plg->GetSamplePtr();

			if( block_ptr ) // IO Mod's Uparameters don't use sample block pointers
			{
				dbg_copy_output_array[p]->Copy( block_ptr );

				if( plg->Direction == DR_IN )
				{
					if( !plg->connections.empty() )
					{
						// Modules using SSE are allowed to overwrite up to 4 samples off end of buffer.
						// This simplifies the SSE logic. SE reserves extra samples for this purpose.
						auto upstreamUg = plg->connections.front()->UG;
						const auto usesSse = (upstreamUg->flags & UGF_SSE_OVERWRITES_BUFFER_END) != 0;
						const auto blockCorrupted = CheckOverwrite(block_ptr, blockSize, usesSse);

						if(blockCorrupted)
						{
							block_ok = false;
							msg = (L"pre-check: input buffer corrupted (off end)");
						}
					}
				}
			}
		}
	}

	if( block_ok == false && (ug->flags & UGF_OVERWRITING_BUFFERS) == 0)
	{
		//_RPT2(_CRT_WARN, "\n******** ", start_pos, start_pos + sampleframes );
		ug->DebugPrintName();
		_RPT0(_CRT_WARN, " corrupted buffers ****** !!!\n" );
		ug->SetFlag(UGF_OVERWRITING_BUFFERS);
		ug->message(msg);
	}
}

void AudioMasterBase::verify_buffers(ug_base* ug, int start_pos, int sampleframes)
{
#if 1 // !!! TODO
	std::wstring msg;
	bool block_ok = true;
	const auto blockSize = BlockSize();
	int end_pos = blockSize;// + USAMPLEBLOCK_SAFETY_ZONE_SIZE;//1; // check into safty zone
	int blocks = (int) ug->plugs.size();

	if( blocks > MAX_DEBUG_BUFFERS )
		blocks = MAX_DEBUG_BUFFERS;

	for( int p = 0 ; p < blocks ; p++ )
	{
		UPlug* plg = ug->plugs[p];

		if( plg->DataType == DT_FSAMPLE)
		{
			if( plg->Direction == DR_OUT )
			{
				const auto* block_ptr = plg->GetSamplePtr();

				if( block_ptr ) // IO Mod's Uparameters don't use sample block pointers
				{
					// Module must never overwrite previous samples.
					if( false == dbg_copy_output_array[p]->Compare(block_ptr, 0, start_pos ) )
					{
						block_ok = false;
						std::wostringstream oss;
						oss << L"Corrupting out buffers (pre-safe zone). Samples 0 -> " << start_pos;
						msg = oss.str();
					}
#if 0
					for (int i = 0; i < start_pos; ++i)
					{
						if (!isfinite(block_ptr[i]))
						{
							_RPT0(0, "INF!!");
						}
						if (isnan(block_ptr[i]))
						{
							_RPT0(0, "NaN!!");
						}
					}
#endif
					int future_samples = start_pos + sampleframes;

					// SSE modules are allowed to overwrite the end to complete a group of 4 aligned samples.
					if( (ug->flags & UGF_SSE_OVERWRITES_BUFFER_END) != 0 )
					{
						future_samples = (future_samples + 3) & 0xfffffc;
					}

					if( false == dbg_copy_output_array[p]->Compare(block_ptr, future_samples, end_pos ) )
					{
						block_ok = false;
						std::wostringstream oss;
						oss << L"Corrupting out buffers(after safe zone). Samples " << (start_pos + sampleframes) << L" -> " << end_pos;
						msg = oss.str();
					}

					// Samples completely off end of buffer. Should never be written, with small leniency for SSE modules.
					auto blockCorrupted = CheckOverwrite(block_ptr, blockSize, (ug->flags & UGF_SSE_OVERWRITES_BUFFER_END) != 0);
					if( blockCorrupted )
					{
						// Modules using SSE are allowed to overwrite up to 4 samples off end of buffer.
						// This simplifies the SSE logic. SE reserves extra samples for this purpose.
						block_ok = false;
						msg = (L"Writting past end of output buffers");
					}
				}
			}
			else // Input pin.
			{
				//				if( ( plg->flags & PF_ADDER) == 0 )
				{
					const auto* sb = plg->GetSamplePtr();

					if( sb != 0 ) // IO mod inputs don't have block
					{
						if( false == dbg_copy_output_array[p]->Compare( sb, 0, end_pos ) )
						{
							block_ok = false;
							msg = (L"Corrupting in buffers. See debug Window..");
						}

						if( !plg->connections.empty() )
						{
							ug_base* ug2 = plg->connections.front()->UG;
							// Modules using SSE are allowed to overwrite up to 4 samples off end of buffer.
							// This simplifies the SSE logic. SE reserves extra samples for this purpose.
							const auto blockCorrupted = CheckOverwrite(sb, blockSize, (ug2->flags & UGF_SSE_OVERWRITES_BUFFER_END) != 0);

							if(blockCorrupted)
							{
								block_ok = false;
								msg = (L"Writting past end of INPUT buffers");
							}
						}
					}
				}
			}
		}
	}

	/*
		for( int o = 0 ; o < ug->GetNumOutputs() ; o ++ )
		{
			if( false == dbg_copy_output_array[o].Compare( &(ug->output _array[o]), 0, start_pos ) )
			{
				block_ok = false;
			}
			if( false == dbg_copy_output_array[o].Compare( &(ug->output _array[o]), start_pos + sampleframes, end_pos ) )
				block_ok = false;
		}
	*/
	if( block_ok == false && (ug->flags & UGF_OVERWRITING_BUFFERS) == 0)
	{
		ug->DebugIdentify();
		_RPT0(_CRT_WARN, " corrupting buffers ****** !!!\n" );
		ug->SetFlag(UGF_OVERWRITING_BUFFERS);
		msg += (L"\n\nLocation: ");
		msg += ug->GetFullPath();
		// Adders don't have CUG (no handle), so can't send messages. Try parent.
		ug_base* u = ug;

		if( u->Handle() == 0xcdcdcdcd )
		{
			u = u->ParentContainer();
		}

		u->message(msg);
	}
#endif
}

void AudioMasterBase::check_out_values(ug_base* ug, int start_pos, int sampleframes)
{
	bool block_ok = true;
	float bad_val = 0.f;
	int bad_out_number = 0;

	for( auto plg : ug->plugs)
	{
		if (plg->DataType != DT_FSAMPLE || plg->Direction != DR_OUT)
			continue;

		const float* o = plg->GetSamplePtr() + start_pos;
				int denormal_count = 0;
				for( int s = sampleframes ; s > 0 ; s-- )
				{
					// this logic returns false if out of range OR if o is infinite
					bool in_range = *o < 10000.f && *o > -10000.f;
					/*
									float test = FLT_MIN;
					//				test = test / 10000000.f; // very small denormal
									test = test / 10.f; // 'big' denormal
					*/
					unsigned int l = *((unsigned int*)(o));

					if( *o != 0.f && ((l & 0x7FF00000) == 0) )
					{
						denormal_count++;
					}

					if( !in_range || denormal_count > 5 )
					{
						block_ok = false;
						bad_val = *o;
						bad_out_number = plg->getPlugIndex();
						break;
					}

					o++;
				}
			}

	if( !block_ok && error_msg_count++ < 30)
	{
			_RPT3(_CRT_WARN, "Module %d Writing pos %d -> %d\n", ug->Handle(), start_pos, start_pos + sampleframes );

			if( !( bad_val > -1.f && bad_val < 1.f ) ) // weird logic to handle infinite
			{
				_RPTW3(_CRT_WARN, L"******** '%s' output %d value out of range (%f)****** !!!\n", ug->DebugModuleName().c_str(), bad_out_number, bad_val );
			}
			else
			{
				_RPTW2(_CRT_WARN, L"******** '%s' output %d value DENORMAL )****** !!!\n", ug->DebugModuleName().c_str(), bad_out_number);
			}
		}
	}
#endif
/*-----------------------------------------------------------------------------
Function: Returns type of IEEE number
Notes:    Signaling NAN's can only occur on Intel's coprocessor when dealing
		  with IEEE 10-byte form. For IEEE 8-byte form all NANs are Quiet NANs
-----------------------------------------------------------------------------*/
/*
int _fpclass(double d)
{
	int fptype;

	unsigned int l = ((unsigned int *)(&d))[1];

	if ((l & 0x7FF00000) == 0x7FF00000)
	{
		/ * +INF, -INF, QNAN * /
		if ((l & 0x000FFFFF) != 0 || ((unsigned int *)(&d))[0] != 0)
			return _FPCLASS_QNAN;
		else
			fptype = _FPCLASS_PINF;

	}
	else if ((l & 0x7FF00000) == 0)
	{
		/ * denormal or 0.0 * /
		if ((l & 0x000FFFFF) != 0 || ((unsigned int *)(&d))[0] != 0)
			fptype = _FPCLASS_PN;
		else
			fptype = _FPCLASS_PD;
	}
	else
		fptype = _FPCLASS_PN;   / * normal * /

	/ * adjust if negative * /
	if ((l & 0x80000000) != 0)
		fptype <<= 1;

	return fptype;
}
*/

int SeAudioMaster::Open( )
{
	DenormalFixer flushDenormals;

	state = audioMasterState::Starting;
//	_RPT0(0, "audioMasterState::Starting\n");

	assert( m_sample_clock == 0 );

	synth_thread_running = true;
	synth_thread_started = true; // this one stays true, synth_thread_running can happen too fast for UI thread to register

	SetAudioMaster( this );
	// initialise variables
	SetSampleClock(0); // base class private sample clock (duplication?)
	m_sample_clock = 0;

	// from constructor
	next_master_clock = 0;
	current_run_ug = nullptr;
	block_start_clock = 0;
	// AudioDevs = NULL;
	activeModules.clear();
#ifdef _DEBUG
	dbg_copy_output_array.assign(MAX_DEBUG_BUFFERS,(USampBlock*)0);

	for( int i = 0 ; i < MAX_DEBUG_BUFFERS ; ++i )
	{
		dbg_copy_output_array[i] = new USampBlock( BlockSize() );
	}

#endif
	AssignPinBuffers();

	// initialise variables
	main_container->SetAudioMaster(this);
	main_container->Open(); // Open all unit generators.

	activeModules.SortAll(nonExecutingModules);

	assert(state == audioMasterState::Starting);

	state = audioMasterState::Running;
//	_RPT0(0, "audioMasterState::Running\n");

	return 0;
}

void SeAudioMaster::UpdateUI()
{
	my_msg_que_output_stream strm( getShell()->MessageQueToGui(), Handle(), "refr");
	strm << (int) 0; // message length.
	strm.Send();
}

void SeAudioMaster::Close()
{
	if (main_container)
		main_container->Close();	// Close all UGs

	// Inform UI thread that sound generation has terminated
	// UI may not be aware
	synth_thread_running = false; // inform UI it is safe to delete all
}

#ifdef _DEBUG
int debug_count = 100;
#endif

// SEP attempting to call VST Host
int SeAudioMaster::CallVstHost(int opcode, int index, int value, void* ptr, float opt)
{
	return -1;
}

void SeAudioMaster::setBufferSize(int driver_buffer_size)
{
	setBlockSize(CalcBlockSize(driver_buffer_size));

	const float module_cpu_meter_rate = 25.f; // hz
	float temp = 0.5f + SampleRate() / (module_cpu_meter_rate * BlockSize());
	cpu_block_rate = (int)temp;

	if (cpu_block_rate < 1)
		cpu_block_rate = 1;
}

int SeAudioMaster::CalcBlockSize(int p_buffersize)
{
	// try to find a block size that divides nicly into the audio buffer size (not nesc possible)
	int actual_block_size = getOptimumBlockSize();

	if( p_buffersize < actual_block_size ) // if driver buffer less than 96 samples, simply use that
	{
		actual_block_size = p_buffersize;
	}
	else
	{
		// search higher..
		int approx_divisor = p_buffersize / getOptimumBlockSize();
		int i = approx_divisor;

		for( ; i >= 1 ; i-- )
		{
			if( 0 == (p_buffersize % i) )
			{
				actual_block_size = p_buffersize / i;
				i = 0;
			}
		}

		// search lower..
		int search_to = getOptimumBlockSize() * 2 - actual_block_size;
		search_to = max(search_to,30); // don't want blocks smaller than 30
		search_to = p_buffersize / search_to;

		for( i = approx_divisor + 1 ; i < search_to ; i++ )
		{
			if( 0 == (p_buffersize % i) )
			{
				actual_block_size = p_buffersize / i;
				i = 999999;
			}
		}
	}

#ifdef _DEBUG

//	actual_block_size--; // stress TESTING non-SSE buffer sizes !!!!!!!!!

	int blocks_per_buffer = p_buffersize / actual_block_size; //(p_buffersize + ideal_block_size - 1 ) / ideal_block_size;
	bool not_multiple_of_buffer = 0 != (p_buffersize % actual_block_size);

	if( not_multiple_of_buffer )
	{
		_RPT3(_CRT_WARN, "BLOCK SIZE %d, DRIVER BUFFER %d (%d per buffer, !!NOT EXACT!!)\n", actual_block_size,p_buffersize,blocks_per_buffer );
	}
	else
	{
		_RPT3(_CRT_WARN, "BLOCK SIZE %d, DRIVER BUFFER %d (%d per buffer, EXACT)\n", actual_block_size,p_buffersize,blocks_per_buffer );
	}

#endif

	return actual_block_size;
}

dsp_msg_target* SeAudioMaster::HandleToObject(int p_handle)
{
	auto it = m_handle_map.find(p_handle);

	if ( it != m_handle_map.end( ) )
		return(*it).second;

	return 0;
}

// objects without document object (created at run time like ug_patch_param_watcher)
// need a handle. Assign a negative one (so it won't clash with other handles)
// these handles not persistent, are forgotten whenever synth engine destroyed.
// see also: UniqueSnowflakeOwner::GenerateUniqueHandleValue()
void SeAudioMaster::AssignTemporaryHandle(dsp_msg_target* p_object)
{
	RegisterDspMsgHandle( p_object, temporaryHandle_-- );
}

void SeAudioMaster::RegisterDspMsgHandle(dsp_msg_target* p_object, int p_handle)
{
	// Any module with two generators will get registerd twice (should not be a prob once ug_base has handle removed)
	//	_RPT2(_CRT_WARN, "RegisterDspMsgHandle %d %d\n", p_object, p_handle);
	//	assert( m_handle_map.find(p_handle) == m_handle_map.end() );
	m_handle_map.insert({ p_handle, p_object });
	p_object->SetHandle(p_handle);
}

void SeAudioMaster::UnRegisterDspMsgHandle(int p_handle)
{
	m_handle_map.erase(p_handle);
}

void AudioMasterBase::CpuFunc()
{
	const auto cpu_block_rate_f = static_cast<float>(cpu_block_rate);

	// include active modules's CPU in parent containers.
	for (auto it = activeModules.inclusiveBegin(); it != activeModules.inclusiveEnd(); ++it)
	{
		((ug_base*)*it)->SumCpu(cpu_block_rate_f);
	}

	for (auto m : nonExecutingModules)
	{
		m->SumCpu(cpu_block_rate_f);
	}

	// pass container's CPU to any parent. Containers are never on active list.
	for (auto ug : m_cpu_parents)
	{
		ug->OnCpuMeasure(cpu_block_rate_f);
	}

	// Debug Windows.
	for (auto debugger : m_debuggers)
	{
		debugger->CpuToGui();
	}
}

void SeAudioMaster::CpuFunc()
{
	AudioMasterBase::CpuFunc();

	// TODO:: eliminate RunDelayed, just use a dedicated event for CPU metering.
	RunDelayed(SampleClock() + BlockSize() * cpu_block_rate, static_cast <ug_func> (&SeAudioMaster::CpuFunc));
}

lookup_table_entry::lookup_table_entry( void* p_pluginInstance, std::wstring p_name, int p_sample_rate, bool integer_table, size_t p_size ) :
name( p_name )
,sample_rate(p_sample_rate)
,lookup_table(NULL)
,pluginInstance(p_pluginInstance)
{
	if( integer_table )
	{
		lookup_table = new ULookup_Integer;
		((ULookup_Integer*)lookup_table)->SetSize((int)p_size);
	}
	else
	{
		lookup_table = new ULookup;
		((ULookup*)lookup_table)->SetSize((int)p_size);
	}
}

lookup_table_entry::~lookup_table_entry()
{
	delete lookup_table;
}

// return pointer to requested table in 'lookup_ptr' by reference.
void SeAudioMaster::ugCreateSharedLookup(const std::wstring& id, LookupTable** lookup_ptr, int sample_rate, size_t p_size, bool integer_table, bool create, SharedLookupScope scope  )
{
	//	_RPTW3(_CRT_WARN, L"ugCreateSharedLookup '%s' this=%x thread=%d\n", id, this, GetCurrentThreadId() );
	// prevent lookuptable being created twice.
	// has this table already been created?
	assert( sample_rate > 0.f || sample_rate == -1.f ); // -1 indictes not dependant on samplerate

	// Needs lock in VST ( accessing statics ).
    gmpi_sdk::AutoCriticalSection cs(audioMasterLock_);

	void* lPluginInstance;
	if( scope == SLS_ONE_MODULE )
	{
		lPluginInstance = (void*) m_shell; // prevent multiple VST plugin instances sharing data
	}
	else
	{
		lPluginInstance = 0; // allow multiple plugin instances sharing data
	}

	for( auto it = lookup_tables.begin() ; it != lookup_tables.end() ; ++it )
	{
		lookup_table_entry* tableEntry = *it;

		if( tableEntry->name == id && sample_rate == tableEntry->sample_rate && tableEntry->pluginInstance == lPluginInstance )
		{
			// return pointer by reference
			*lookup_ptr = tableEntry->lookup_table;

			if (tableEntry->lookup_table->GetSize() != p_size && create)
			{
				wstring msg( L"MODULE ERROR: Shared Memory allocation: Existing table wrong size for new allocation. Mem ID: " );
				msg += tableEntry->name;
				ugmessage( msg );
			}

			/* not with oversampling
						// if this table was calculated for a diferent sample rate
						// it needs to be re-initialised
						if( sample_rate != lookup_tables.GetAt(j)->sample_rate )
						{
							lookup_tables.GetAt(j)->lookup_table->ClearInitialised();
							lookup_tables.GetAt(j)->sample_rate = sample_rate;
						}
			*/
			return;
		}
	}

	*lookup_ptr = 0;

	if( create )
	{
		// not found, create a new one
		lookup_table_entry* lu = new lookup_table_entry( lPluginInstance, id, sample_rate, integer_table, p_size );

		if( lu->lookup_table->GetSize() == p_size )
		{
			lookup_tables.push_back( lu );

			// return pointer by reference
			*lookup_ptr = lookup_tables.back()->lookup_table;
		}
		else
		{
			// Allocation failed.
			delete lu;
		}
	}
}

void SeAudioMaster::RegisterPatchAutomator(class ug_patch_automator* client)
{
	patchAutomators_.push_back(client);
	getShell()->NeedTempo();
}

void SeAudioMaster::UpdateTempo( my_VstTimeInfo* timeInfo )
{
	if ((timeInfo->flags & my_VstTimeInfo::kVstTempoValid) == 0)
	{
		timeInfo->tempo = 120.0f;
	}

	if ((timeInfo->flags & my_VstTimeInfo::kVstTimeSigValid) == 0)
	{
		timeInfo->timeSigNumerator = timeInfo->timeSigDenominator = 4;
	}

	if (!(timeInfo->tempo > 0.0))
	{
		std::cerr << "ERROR: DAW Tempo Invalid" << std::endl;
	}

	if (timeInfo->timeSigNumerator <= 0 || timeInfo->timeSigDenominator <= 0)
	{
		std::cerr << "ERROR: DAW time signature Invalid" << std::endl;
	}

	// Fix for AU Validation which passes ppqPos = inf
#ifndef _MSC_VER
	if (!isfinite(timeInfo->ppqPos))
	{
		timeInfo->ppqPos = 0.0;
	}
#endif

	for( auto client : patchAutomators_)
	{
		client->UpdateTempo(timeInfo);
	}
}

void SeAudioMaster::SetHostControl( int hostConnect, int32_t value )
{
	for( auto client : patchAutomators_)
	{
		auto param = client->patch_control_container->get_patch_manager()->GetHostControl(hostConnect); // very messy.
		if(param)
		{
			if (param->SetValueRaw2(&value, sizeof(value), 0, 0))
			{
				param->OnValueChangedFromGUI(false, 0);
			}
		}
	}
}

timestamp_t SeAudioMaster::CalculateOversampledTimestamp( ug_container* top_container, timestamp_t timestamp )
{
	assert( false && "Failed to find top-container" );
	return timestamp;
}

int SeAudioMaster::getLatencySamples()
{
	assert(audioOutModule);
	return audioOutModule->getOverallPluginLatencySamples();
}

int SeAudioMaster::getNumInputs()
{
	return audioInModule->getAudioInputCount();
}

int SeAudioMaster::getNumOutputs()
{
	return audioOutModule->getAudioOutputCount();
}

bool SeAudioMaster::wantsMidi()
{
	return audioInModule->wantsMidi();
}

bool SeAudioMaster::sendsMidi()
{
	return audioOutModule->sendsMidi();
}

class MidiBuffer3* SeAudioMaster::getMidiOutputBuffer()
{
	return audioOutModule->getMidiOutputBuffer();
}

void SeAudioMaster::RegisterBypassableModule(ug_base* m)
{
	patchCableModules.push_back(m);
}

void SeAudioMaster::CleanupBypassableModules()
{
	for (auto m : patchCableModules)
	{
		int voice = m->GetPolyphonic() ? 1 : 0;
		auto u = m;
		while (u) // for each voice.
		{
			auto next = u->m_next_clone;
			u->BypassRedundentModules(voice); // u may delete itself.
			u = next;
			++voice;
		}
	}

	patchCableModules = std::move( std::vector<ug_base*>() ); // clear it.
}

void SeAudioMaster::RegisterConstantPin(UPlug* pin, float fval)
{
	audioPinConstants[fval].push_back(pin);
}

void SeAudioMaster::RegisterConstantPin(UPlug* pin, const char* utf8val)
{
	const auto fval = 0.1f * (float) atof(utf8val);
	RegisterConstantPin(pin, fval);
}

float SeAudioMaster::getConstantPinVal(UPlug* orig)
{
	for (auto& valpins : audioPinConstants)
	{
		for (auto p : valpins.second)
		{
			if (p == orig)
			{
				return valpins.first;
			}
		}
	}

	assert(false); // didn't find clones original pin
	return {};
}

void SeAudioMaster::RegisterPin(UPlug* pin)
{
	audioPins.push_back(pin);
}

void SeAudioMaster::UnRegisterPin(UPlug* pin)
{
	audioPins.erase(std::remove(audioPins.begin(), audioPins.end(), pin), audioPins.end());
}

void SeAudioMaster::AssignPinBuffers()
{
	const auto bufferSizeForDriver = BlockSize();

	// if needed, allocate a few extra to ensure all buffers are aligned nicely for SIMD (4 floats)
	constexpr int simdSize = 4;

#ifdef _DEBUG
	constexpr int overwriteCheckSize = simdSize;
#else
	constexpr int overwriteCheckSize = 0;
#endif

	const auto simdFriendlyBufferSize = overwriteCheckSize + (bufferSizeForDriver + simdSize - 1) & ~(simdSize - 1);

	const size_t audioBufferCount = audioPinConstants.size() + audioPins.size();

	audioBuffers.resize(audioBufferCount * simdFriendlyBufferSize);

#ifdef _DEBUG
	// fill safety bytes with magic_guard_number
	for (int j = 0; j < audioBufferCount; ++j)
	{
		for (int i = bufferSizeForDriver; i < simdFriendlyBufferSize; ++i)
		{
			audioBuffers[simdFriendlyBufferSize * j + i] = magic_guard_number;
		}
	}
#endif

	float* dest = audioBuffers.data();
	for (auto cst : audioPinConstants)
	{
		// initialize buffer values.
		for (int i = 0; i < bufferSizeForDriver; ++i)
		{
			dest[i] = cst.first;
		}

		// point all plugs to the buffer
		for (auto p : cst.second)
		{
			p->AssignBuffer(dest);
		}

		dest += simdFriendlyBufferSize;
	}

	std::sort(
		audioPins.begin(),
		audioPins.end(),
		[](const UPlug* p1, const UPlug* p2)
		{
			if (p1->UG->SortOrder != p2->UG->SortOrder)
			{
				return p1->UG->SortOrder < p2->UG->SortOrder;
			}

            if (p1->getPlugIndex() != p2->getPlugIndex())
				return p1->getPlugIndex() < p2->getPlugIndex();
            
            return p1 < p2;
		}
	);

	for (auto p : audioPins)
	{
		p->AssignBuffer(dest);
		dest += simdFriendlyBufferSize;
	}
}

void SeAudioMaster::getPresetsState(std::vector< std::pair<int32_t, std::string> >& presets, bool saveRestartState)
{
	for (auto pa : patchAutomators_)
	{
		auto cont = pa->patch_control_container;
		auto patchmanager = cont->get_patch_manager(); // very messy.

		presets.push_back({ cont->Handle() , {} });

		patchmanager->getPresetState(presets.back().second, saveRestartState);
	}
}

void SeAudioMaster::setPresetsState(const std::vector< std::pair<int32_t, std::string> >& presets)
{
	for (const auto& preset : presets)
	{
		auto cont = dynamic_cast<ug_container*>(HandleToObject(preset.first));
		if (!cont)
			continue;

		auto patchmanager = cont->get_patch_manager();
		patchmanager->setPresetState(preset.second);
	}
}

#ifdef _DEBUG
#include <filesystem>
void SeAudioMaster::dumpPreset(int tag)
{
	int index = 0;
	for (auto pa : patchAutomators_)
	{
		auto cont = pa->patch_control_container;
		auto patchmanager = cont->get_patch_manager(); // very messy.

		std::string chunk;
		patchmanager->getPresetState(chunk, true);

		std::filesystem::path saveFileName = std::format("C:\\temp\\preset_dump_{}_{}.txt", tag, index);

		std::ofstream outFile(saveFileName, std::ios::binary);
		if (outFile.is_open())
		{
			outFile << chunk;
			outFile.close();
			_RPT1(_CRT_WARN, "Preset dumped to %s\n", saveFileName.string().c_str());
		}
		else
		{
			_RPT0(_CRT_WARN, "Failed to open file for preset dump\n");
		}

		index++;
	}
}
#endif

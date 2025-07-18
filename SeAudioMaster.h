// SeAudioMaster.h: interface for the SeAudioMaster class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_SEAUDIOMASTER_H__9F4E5251_C0C6_11D4_B6EE_00104B15CCF0__INCLUDED_)
#define AFX_SEAUDIOMASTER_H__9F4E5251_C0C6_11D4_B6EE_00104B15CCF0__INCLUDED_

#pragma once

#include <map>
#include <unordered_map>
#include <list>
#include <vector>
#include <iostream>
#include <fstream>
#include <chrono>
#include <atomic>
#include "EventProcessor.h"
#include "interThreadQue.h"
#include "./dsp_msg_target.h"
#include "./EventProcessor.h"
#include "./ISeAudioMaster.h"
#include "./IDspPatchManager.h"
#include "ug_event.h"
#include "./modules/shared/xplatform.h"
#include "ElatencyContraintType.h"
#include "cancellation.h"
#include "my_VstTimeInfo.h"
#include "iseshelldsp.h"
#include "CpuConsumption.h"
#include "HoverScopeAudioCollector.h"

// Waves-only, use fixed-pool memory management.
#if defined( SE_FIXED_POOL_MEMORY_ALLOCATION )
#include "fixed_memory_manager.h"
#endif

class UPlug;
class ug_container;
class ug_base;

class USampBlock;
class CDocOb;

// debug flags values. ref AudioMasterBase::GetDebugFlag()
#define DBF_TRACE_POLY				1
#define DBF_RANDOMISE_BLOCK_SIZE	2
#define DBF_BLOCK_CHECK				4
#define DBF_DS_EXCLUSIVE			8
// DBF_OUT_VAL_CHECK requires SE_CHECK_BUFFERS
#define DBF_OUT_VAL_CHECK			16
#define SET_PARAMETER_QUE_SIZE		1024

// table used to interpolate the sample points
#define INTERPOLATION_POINTS 8
#define INTERPOLATION_DIV 32

class ug_soundcard_out;
class dsp_msg_target;
class IO_base;
class my_msg_que_input_stream;
class my_input_stream;
struct DawPreset;

// helper struct to track lookup table list
struct lookup_table_entry
{
	class LookupTable* lookup_table;
	int sample_rate;
	void* pluginInstance; // Use Audiomaster address as proxy for VST instance.
	std::wstring name;

	lookup_table_entry( void* p_pluginInstance, std::wstring p_name, int p_sample_rate, bool integer_table, size_t p_size);
	~lookup_table_entry();
};

class LookupTables : public std::vector<lookup_table_entry*>
{
public:
	~LookupTables();
};

class ModuleContainer
{
	std::vector< EventProcessor* > modules;
public:

	static EventProcessor* safePointer(EventProcessor* e)
	{
		constexpr intptr_t mask = ((intptr_t)-1) ^ (intptr_t)1;
		return reinterpret_cast<EventProcessor*>(reinterpret_cast<intptr_t>(e) & mask);
	}

	// iterate all *active* modules, skipping ones that are asleep.
	class iterator
	{
		ModuleContainer* container_ = nullptr;
	public:
		std::vector< EventProcessor* >::iterator it_;

		iterator(ModuleContainer* container) :
			container_(container)
		{
		}
		EventProcessor* operator*() const
		{
			return *it_;
		}
		iterator& operator++()
		{
			++it_;
			skipSleepers();
			return *this;
		}

		bool equal(iterator const& rhs) const
		{
			return it_ == rhs.it_;
		}

		bool operator==(const iterator& other)
		{
			return it_ == other.it_;
		}

		bool operator!=(const iterator& other)
		{
			return !operator==(other);
		}

		void skipSleepers()
		{
			while (it_ != container_->modules.end())
			{
				if ((reinterpret_cast<intptr_t>(*it_) & 1) == 0)
					break;
				++it_;
			}
		}
	};

	// iterate all modules, including ones that are asleep.
	class inclusive_iterator
	{
	public:
		std::vector< EventProcessor* >::iterator it_;

		inclusive_iterator()
		{
		}
		EventProcessor* operator*() const
		{
			return safePointer(*it_);
		}
		inclusive_iterator& operator++()
		{
			++it_;
			return *this;
		}

		bool equal(inclusive_iterator const& rhs) const
		{
			return it_ == rhs.it_;
		}

		bool operator==(const inclusive_iterator& other)
		{
			return it_ == other.it_;
		}

		bool operator!=(const inclusive_iterator& other)
		{
			return !operator==(other);
		}
	};

	// iterate all *active* modules, skipping ones that are asleep.
	iterator begin()
	{
		iterator i(this);
		i.it_ = modules.begin();
		i.skipSleepers();
		return i;
	}
	iterator end()
	{
		iterator i(this);
		i.it_ = modules.end();
		return i;
	}

	// iterate all modules, including ones that are asleep.
	auto inclusiveBegin()
	{
		inclusive_iterator i;
		i.it_ = modules.begin();
		return i;
	}

	auto inclusiveEnd()
	{
		inclusive_iterator i;
		i.it_ = modules.end();
		return i;
	}

	void flagPointer(EventProcessor*& e)
	{
		constexpr intptr_t flagBit = 1;
		e = reinterpret_cast<EventProcessor*>(reinterpret_cast<intptr_t>(e) | flagBit);
	}

	void insertSorted(EventProcessor* e)
	{
		if(e->moduleContainerIndex >= 0)
		{
			modules[e->moduleContainerIndex] = safePointer(modules[e->moduleContainerIndex]); // flag pointer as disabled.
		}
		else
		{
			e->moduleContainerIndex = static_cast<int>(modules.size());
			modules.push_back(e); // Assume we will sort it properly later.
		}
	}

	void SortAll(std::vector<ug_base*>& nonExecutingModules);

	void erase(EventProcessor* e)
	{
		assert(e->moduleContainerIndex >= 0);
		{
			flagPointer(modules[e->moduleContainerIndex]);
		}
	}

	void clear()
	{
		for (auto& it : modules)
		{
			flagPointer(it);
		}
	}
};

class AudioMasterBase : public ISeAudioMaster
{
public:
	AudioMasterBase() :
		main_container(nullptr)
#if defined( _DEBUG )
			,error_msg_count(0) // prevent 'floods' of msgs to debug window
#endif
			{};

	virtual ~AudioMasterBase();

	void CancellationFreeze(timestamp_t sample_clock);
#ifdef CANCELLATION_TEST_ENABLE2
	void CancellationFreeze2(timestamp_t sample_clock);
	void WriteCancellationData(int32_t blockSize, FILE* file);
	bool cancellation_done = false;
#endif

	void processModules_plugin(
		int lBlockPosition
		, int sampleframes
#ifdef _DEBUG
		, timestamp_t sample_clock
#endif
	);

	void processModules_editor(
		int lBlockPosition
		, int sampleframes
#ifdef _DEBUG
		, timestamp_t sample_clock
#endif
	);

	void BuildModules(
		ug_container* container,
		ug_container* patch_control_container,
		class TiXmlElement* xml,
		ug_base* cpu_parent,
		std::vector< std::pair<int32_t, std::string> >& pendingPresets,
		std::vector<int32_t>& mutedContainers
	);
	static int calcOversampleFactor(int oversampleFactor, int sampleRate, bool hdMode = true);

	void RegisterModuleDebugger(class UgDebugInfo* module) override
	{
		m_debuggers.push_back(module);
	}
	void AddCpuParent(ug_base* module) override
	{
		m_cpu_parents.push_back(module);
	}

protected:
	ModuleContainer activeModules;
	std::vector<ug_base*> nonExecutingModules; // these containers are not executed, but they are needed for the CPU measurement on the GUI to work.

public:
	ug_container* main_container;
    
#if defined( _DEBUG )
    std::vector<USampBlock*> dbg_copy_output_array;
    void copy_buffers(ug_base* ug);
	void checkBuffers(EventProcessor * ug, timestamp_t sample_clock, int debug_start_pos, int sampleframes, bool check_blocks);
	void verify_buffers( ug_base* ug, int start_pos, int sampleframes );
    void check_out_values(ug_base* ug, int start_pos, int sampleframes);
	void CheckForOutputOverwrite();
	static bool GetDebugFlag(int flag);

    int error_msg_count; // prevent 'floods' of msgs to debug window
#endif
	void CpuFunc();

	EventProcessor* current_run_ug = nullptr;

#if defined( _DEBUG )
	std::vector< std::string > debug_missing_modules;
#endif

#if defined(LOG_PIN_EVENTS )
	std::ofstream eventLogFile;
#endif
	// editor only
	std::list<class UgDebugInfo*> m_debuggers;
	std::vector<class ug_base*> m_cpu_parents;
	int cpu_block_rate = 1;
	std::unique_ptr<HoverScopeAudioCollector> hoverScopeModule;
};

class SeAudioMaster : public EventProcessor, public interThreadQueUser, public AudioMasterBase
{
	friend class AudioMasterBase;

public:
	// this need to be fairly big, if it fills up, audio thread will block until GUI thread clears que.
	// UPDATE: Now not using overflow (stricter realtime? not sure why). Using larger buffer.
	static const int AUDIO_MESSAGE_QUE_SIZE = 0x500000; // see also UI_MESSAGE_QUE_SIZE2

	SeAudioMaster( float p_samplerate, class ISeShellDsp* p_shell, ElatencyContraintType latencyConstraint = ElatencyContraintType::Off);
	virtual ~SeAudioMaster();

	static int getOptimumBlockSize();
	void setMpeMode(int32_t mpeOn);

	void DoProcess_plugin(int sampleframes, const float* const* inputs, float* const* outputs, int numInputs, int numOutputs);
	void DoProcess_editor(int sampleframes, const float* const* inputs, float* const* outputs, int numInputs, int numOutputs);

	// Plugin-specific
	void getPresetsState(std::vector< std::pair<int32_t, std::string> >& presets, bool saveRestartState);
	void setPresetsState(const std::vector< std::pair<int32_t, std::string> >& presets);
	void getPresetState(std::string& chunk, bool saveRestartState)
    {
        Patchmanager_->getPresetState(chunk, saveRestartState);
    }
#ifdef _DEBUG
	void dumpPreset(int tag);
#endif

    void setPresetState(const std::string& chunk)
    {
        Patchmanager_->setPresetState(chunk);
    }
	void setParameterNormalizedDsp( int timestamp, int paramIndex, float value, int32_t flags);
	void setParameterNormalizedDaw(int timestamp, int32_t paramHandle, float value, int32_t flags);

	// plugin-specific
	void SetupVstIO();
	void MidiIn( int delta, unsigned int shortMidiMsg )
	{
		MidiIn( delta, (unsigned char*) &shortMidiMsg, 3 );
	}
	void MidiIn( int delta, const unsigned char* MidiMsg, int length );

	int getNumInputs();
	int getNumOutputs();
	bool wantsMidi();
	bool sendsMidi();
	class MidiBuffer3* getMidiOutputBuffer();
	void setInputSilent(int input, bool isSilent);
	uint64_t getSilenceFlags(int output, int count);

#if 0
	void BuildDspGraph(
		const char* structureXml,
		std::vector< std::pair<int32_t, std::string> >& pendingPresets,
		std::vector<int32_t>& mutedContainers
	);
#endif

	void BuildDspGraph(
		class TiXmlDocument* doc,
		std::vector< std::pair<int32_t, std::string> >& pendingPresets,
		std::vector<int32_t>& mutedContainers
	);

	ISeShellDsp* getShell() override
	{
		return m_shell;
	}

	int Open();

	void Close();
	void end_run();
	void InsertBlockDev(EventProcessor* p_dev) override;
	void SuspendModule(ug_base* p_module) override;
#ifdef _DEBUG
	void Wake(EventProcessor* ug) override;
#else
	inline void Wake(EventProcessor* ug) override
	{
		if( ug->sleeping ) // may not be
		{
			// can't do, but may be nesc for emulated ugs:	timestamp_t bsc = ug->m_audio_master->Block StartClock(); // avoid overloaded emulator block stat clock
			// done in InsertBlockDev:	ug->SetSampleClock( Block StartClock() );
			InsertBlockDev( ug );
		}
	}
#endif

	// communication
	void ugmessage(std::wstring msg, int type = 0);
	void TriggerRestart();
	void TriggerShutdown();
	void TriggerInterrupt()
	{
		interrupt_flag.store(true, std::memory_order_release);
	}
	void ClearDelaysUnsafe();
	void HandleEvent(SynthEditEvent* /*e*/) override;
	void HandleInterrupt();
	void AssignTemporaryHandle(dsp_msg_target* p_object) override;
	bool onQueMessageReady( int handle, int msg_id, my_input_stream& p_stream ) override;

	// timing
	timestamp_t SampleClock() override
	{
		return m_sample_clock;
	}
	timestamp_t BlockStartClock() override
	{
		return block_start_clock;
	}
	int latencyCompensationMax() override;
	bool SilenceOptimisation() override
	{
		return enableSleepOptimisation;
	}

	int getBlockPosition() override
	{
		return (int) (m_sample_clock-block_start_clock);
	}
	// let a ug know when the next block starts (for all ug's)
	// provides a guaranteed near-future event time across ugs
	timestamp_t NextGlobalStartClock() override
	{
		return next_master_clock;
	}
	static int CalcBlockSize(int p_buffersize);
	
	inline int BlockSize() override
	{
		return m_blocksize;
	}
	// The Audio Driver buffer size. Typically a multiple of SE block-size.
	void setBufferSize(int driver_buffer_size);

	// SynthEdit's internal buffer size. Typically smaller than driver driver size.
	void setBlockSize(int p_bs)
	{
		m_blocksize = p_bs;
	}

	inline float SampleRate() override
	{
		return m_samplerate;
	}
	void setSampleRate(float sr )
	{
		m_samplerate = sr;
	}
	void setSilenceOptimisation(bool v)
	{
		enableSleepOptimisation = v;
	}

	int CallVstHost(int opcode, int index = 0, int value = 0, void* ptr = 0, float opt = 0) override;
	void ugCreateSharedLookup(const std::wstring& id, LookupTable** lookup_ptr, int sample_rate, size_t p_size, bool integer_table, bool create = true, SharedLookupScope scope = SLS_ONE_MODULE ) override;
	void RegisterPatchAutomator( class ug_patch_automator* client ) override;
	timestamp_t CalculateOversampledTimestamp( ug_container* top_container, timestamp_t timestamp ) override;

	// Processor -> Plugin.
	void UpdateTempo( my_VstTimeInfo* timeInfo );
	void SetHostControl( int hostConnect, int32_t value );

	void RegisterBypassableModule(ug_base* m);
	void CleanupBypassableModules();
	void RegisterPin(UPlug* pin);
	void UnRegisterPin(UPlug* pin);
	void RegisterConstantPin(UPlug* pin, float fval);
	void RegisterConstantPin(UPlug* pin, const char* utf8val);
	float getConstantPinVal(UPlug* orig);
	void AssignPinBuffers();

#if defined( SE_FIXED_POOL_MEMORY_ALLOCATION )
	struct SynthEditEvent* AllocateMessage(timestamp_t p_timeStamp = 0, int32_t p_eventType = 0, int32_t p_parm1 = 0, int32_t p_parm2 = 0, int32_t p_parm3 = 0, int32_t p_parm4 = 0, char* extra = 0)
	{
		return memorymanager_.AllocateMessage( p_timeStamp, p_eventType, p_parm1, p_parm2, p_parm3, p_parm4, extra );
	}

	void DeAllocateMessage(SynthEditEvent* e)
	{
		memorymanager_.DeAllocateMessage( e );
	}
	char* AllocateExtraData(uint32_t lSize)
	{
		return memorymanager_.AllocateExtraData( lSize );
	}
	void DeAllocateExtraData(char* pExtraData)
	{
		memorymanager_.DeAllocateExtraData( pExtraData );
	}
#endif // SE_FIXED_POOL_MEMORY_ALLOCATION

	std::wstring GetAudioPath();
	std::wstring GetMidiPath();

	// messages from GUI -> Audio
	void RegisterDspMsgHandle(dsp_msg_target* p_object, int p_handle) override; // new way
	void UnRegisterDspMsgHandle(int p_handle) override;
	dsp_msg_target* HandleToObject(int p_handle) override;
	void ServiceGuiQue() override;

	// dsp_msg_target support.
	void OnUiMsg(int p_msg_id, my_input_stream& p_stream) override;

	// ISeAudioMaster support.
	void onFadeOutComplete() override;

	void SetModuleLatency(int32_t handle, int32_t latency) override;
	void CpuFunc();
	int32_t RegisterIoModule(class ISpecialIoModule* m) override;
	void ApplyPinDefaultChanges(std::unordered_map<int64_t, std::string>& extraPinDefaultChanges);
	int getLatencySamples();
	// debugging
	void UpdateCpu(int64_t elapsed);

	void setPresetState_UI_THREAD(const std::string& chunk, bool processorActive);
	void getPresetState_UI_THREAD(std::string& chunk, bool processorActive, bool saveRestartState);
	void setPresetStateDspHelper();
	void getPresetStateDspHelper();

	timestamp_t m_sample_clock = {};
	timestamp_t block_start_clock;
	timestamp_t next_master_clock;
	timestamp_t host_block_start_clock;

	std::atomic<bool> interrupt_flag = {};
	std::atomic<bool> interupt_start_fade_out = {};
	std::atomic<bool> interupt_module_latency_change = {};
	std::atomic<DawPreset const*> interrupt_preset_ = {};
	bool interrupt_clear_delays = false;

	bool synth_thread_running;
	bool synth_thread_started;
	static int profileBlockSize;
	std::atomic<audioMasterState> state = audioMasterState::Stopped;

protected:
	void UpdateUI();

	ISeShellDsp* m_shell;
	my_VstTimeInfo m_vst_time_info;
	int m_blocksize;
	float m_samplerate;

private:
	std::map<int,dsp_msg_target*> m_handle_map; // maps handle to module address
	int temporaryHandle_;

	IDspPatchManager* Patchmanager_ = {}; // Plugin only

	static LookupTables lookup_tables;
	std::vector<class ug_patch_automator*> patchAutomators_;
	int32_t maxLatency;
	bool enableSleepOptimisation;

// Waves-only, use fixed-pool memory management.
#if defined( SE_FIXED_POOL_MEMORY_ALLOCATION )
	FixedPoolMemoryManager memorymanager_;
#endif

	std::vector<ug_base*> patchCableModules;
	int guiFrameRateSamples = 0;

	std::vector<float> audioBuffers;
	//                 value               pin
	std::unordered_map<float, std::vector<UPlug*> > audioPinConstants; // todo free memory when done !!!
	//      sort-order         pin
	std::vector<UPlug*> audioPins;

	ISpecialIoModuleAudioOut* audioOutModule = {};
	ISpecialIoModuleAudioIn* audioInModule = {};

	// typically editor-only
	uint16_t cpuConsumption[CPU_BATCH_SIZE]; // per block
	int cpuConsumptionIndex = 0;
	std::atomic<bool> interrupt_getchunk_ = {};
	std::atomic<bool> interrupt_setchunk_ = {};
	std::atomic<bool> dsp_getchunk_completed_ = {};
	std::string presetChunkIn_; // store preset from DAW while real-time thread reads it async
	std::string presetChunkOut_; // store preset from processor so DAW can read it async

	int32_t hCClearTailsNextValue = 1;
	UPlug* hoverScopePin{};
};

#endif // !defined(AFX_SEAUDIOMASTER_H__9F4E5251_C0C6_11D4_B6EE_00104B15CCF0__INCLUDED_)

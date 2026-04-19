#pragma once

#include "CVoiceList.h"
#include "ug_base.h"
#include "modules/se_sdk3/mp_midi.h"

class ug_container : public VoiceList, public ug_base
{
public:
	ug_container();
	~ug_container();
	int Open() override;
	int Close() override;
	class ug_patch_param_setter* GetParameterSetter();
	class ug_patch_param_setter* GetParameterSetterSecondary();
	class ug_voice_host_control_fanout* GetVoiceHostControlFanout();
	void ConnectHostControl(HostControls hostConnect, UPlug* plug);

	// MIDI-CV redirector calls here instead of patch_manager->OnMidi. Incoming MIDI is
	// normalised to MIDI 2.0 by midiConverter_, then dispatchMidi2 fires performance events
	// directly via VoiceList::sendDirectPathValue (no patch-manager involvement). Non-
	// performance events (arbitrary CCs for MIDI-Learn, etc.) are silently ignored here —
	// route those through a Patch-Automator.
	void OnMidi(struct VoiceControlState* voiceState, timestamp_t timestamp, const unsigned char* midiMessage, int size);
	ug_base* GetDefaultSetter();
	class ug_patch_param_watcher* GetParameterWatcher();

	ug_base* InsertAdder(EPlugDataType p_data_type);
	ug_base* AddUG(Voice* p_voice,ug_base* p_ug);
	void SortOrderSetup3(int& maxSortOrderGlobal);
	void SetupSecondaryPatchParameterSetter();
	virtual void SetPPVoiceNumber(int n) override;
	DECLARE_UG_BUILD_FUNC(ug_container);

	virtual ug_base* Copy(ISeAudioMaster* audiomaster, CUGLookupList& UGLookupList ) override;
	void ReRoutePlugs() override;
	void PostBuildStuff(bool xmlMethod);
	virtual void IterateContainersDepthFirst(std::function<void(ug_container*)>& f) override;
	void PostBuildStuff_pass2();
	void setupDelayCompensation();
	int calcDelayCompensation() override;
	virtual void CloneContainerVoices() override;
	virtual void CloneConnectorsFrom( ug_base* FromUG, CUGLookupList& UGLookupList ) override;
	ug_container * getVoiceControlContainer();
	void ConnectPatchCables();
	ug_base* AddUG(ug_base* u);
	virtual void Setup(class ISeAudioMaster* am, class TiXmlElement* xml) override;
	void SetupPatchManager(TiXmlElement* patchManager_xml, std::vector<std::pair<int32_t, std::string>>& pendingPresets);
	void BuildPatchManager(class TiXmlElement* patchMgrXml, const std::string* presetXml);
	void BuildAutomationModules();
	void SetUnusedPlugs2();
	void setPatchManager(IDspPatchManager* p_patch_mgr);
	virtual IDspPatchManager* get_patch_manager() override;
	bool has_own_patch_manager();
	virtual void OnUiNotify2( int p_msg_id, gmpi::hosting::my_input_stream& p_stream ) override;
	timestamp_t CalculateOversampledTimestamp( ug_container* top_container, timestamp_t timestamp );
	void DoVoiceRefresh();
	bool belongsTo(ug_container * container);

	void OnCpuMeasure(float cpu_block_rate) override
	{
		SumCpu(cpu_block_rate);
	}
	void SetContainerPolyphonic();
	void RouteDummyPinToPatchAutomator(UPlug* pin);
	
	// controls need to be able to access midi automator if present
	class ug_patch_automator* automation_input_device;
	class ug_patch_automator_out* automation_output_device;

private:
	IDspPatchManager* m_patch_manager;
	class ug_patch_param_setter* parameterSetter_;
	class ug_patch_param_setter* parameterSetterSecondary_;
	class ug_voice_host_control_fanout* voiceHostControlFanout_;
	class ug_patch_param_watcher* parameterWatcher_;

	ug_base* defaultSetter_; // cached for fast access.
	int nextRefreshVoice_;

	// Normalises inbound MIDI to MIDI 2.0 UMP format so OnMidi can dispatch a single branch.
	// The converter holds the RPN/NRPN state machine (per-channel) internally, so MIDI 1.0
	// data-entry CCs (CC 6/38) arrive pre-assembled as full MIDI 2.0 RPN or NRPN messages.
	// Initialised in ug_container() with a sink that forwards to dispatchMidi2.
	gmpi::midi_2_0::MidiConverter2 midiConverter_;

	// Dispatch a single MIDI 2.0 message. Called by midiConverter_'s sink, or (for MIDI Tuning
	// Standard SysEx) directly by OnMidi before the converter. voiceState_ is the VoiceControlState
	// current OnMidi is running on behalf of — cached because the converter's sink signature
	// doesn't carry it.
	struct VoiceControlState* voiceState_ = nullptr;
	void dispatchMidi2(timestamp_t timestamp, gmpi::midi::message_view msg);
};

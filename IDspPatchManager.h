#pragma once
#include <string>
#include "se_types.h"
#include "HostControls.h"
#include "modules/shared/xplatform.h"
#include "modules/se_sdk3/hasMidiTuning.h"

struct VoiceControlState : public hasMidiTuning
{
	static const int maxKeys = 128;

	class ug_container* voiceControlContainer_;

	VoiceControlState() :
		voiceControlContainer_(0)
	{
	}

	virtual void OnKeyTuningChangedA(timestamp_t absolutetimestamp, int MidiKeyNumber, int tune);

	// function returning a reference to an array of 128 ints.
	int* getTuningTable()
	{
		return tuningTable;
	}
};

// flags relating to parameter updates from DAW or MIDI
static inline const int32_t kIsMidiMappedAutomation = 1 << 0;
static inline const int32_t kMustUpdateUi = 1 << 1;

class IDspPatchManager
{
public:
	virtual ~IDspPatchManager() {}
	virtual void InitializeAllParameters() = 0;
	virtual void OnMidi(VoiceControlState* voiceState, timestamp_t timestamp, const unsigned char* midiMessage, int size, bool fromMidiCv) = 0;
	virtual float InitializeVoiceParameters(ug_container* voiceControlContainer, timestamp_t timestamp, class Voice* voice /*int voiceId, bool hardReset*/, bool sendTrigger) = 0;// , bool patchManagerAllocatesVoices) = 0;
	virtual void SendInitialUpdates() = 0;
	virtual void OnUiMsg(int p_msg_id, class my_input_stream& p_stream) = 0;
	virtual void vst_Automation(ug_container* voiceControlContainer, timestamp_t p_clock, int p_controller_id, float p_normalised_value, bool sendToMidiCv = true, bool sendToNonMidiCv = true) = 0;
	virtual void vst_Automation2(timestamp_t p_clock, int p_controller_id, const void* data, int size) = 0;

	// First one takes the VST3 tag, the second takes the handle (used by JUCE which uses indexes not tags).
	// would be nice to combine. handle seems cleaner rather than having the guts of SE worry about vst3 parameter tags (yet not aware of juce indexes)
	virtual	void setParameterNormalized(timestamp_t p_clock, int vstParameterIndex, float newValue, int32_t flags ) = 0;
	virtual	void setParameterNormalizedDaw(timestamp_t p_clock, int32_t paramHandle, float newValue, int32_t flags) = 0;

	virtual void setPreset(struct DawPreset const* preset) = 0;
	virtual void setPresetState( const std::string& chunk, bool overrideIgnoreProgramChange = false) = 0;
	virtual void getPresetState( std::string& chunk, bool saveRestartState) = 0;

	virtual	class dsp_patch_parameter_base* GetHostControl(int32_t hostControl, int32_t attachedToContainerHandle = -1) = 0;
	virtual class dsp_patch_parameter_base* createPatchParameter( int typeIdentifier ) = 0; // from GUI.

	// new.
	virtual	dsp_patch_parameter_base* ConnectHostControl(HostControls hostConnect, class UPlug* plug) = 0;
	virtual	void ConnectHostControl2(HostControls hostConnect, UPlug * toPlug) = 0;
	virtual	dsp_patch_parameter_base* ConnectParameter(int parameterHandle, UPlug* plug) = 0;
	virtual	struct FeedbackTrace* InitSetDownstream(ug_container * voiceControlContainer) = 0;
	virtual void setMidiChannel( int c ) = 0;
	virtual int getMidiChannel( ) = 0;
	virtual void setMidiCvVoiceControl() = 0;
	virtual void vst_setAutomationId(dsp_patch_parameter_base* p_param, int p_controller_id) = 0;
	virtual class ug_container* Container() = 0;
	virtual void setupContainerHandles(ug_container* subContainer) = 0;
	virtual dsp_patch_parameter_base* GetParameter( int moduleHandle, int paramIndex ) = 0;
};

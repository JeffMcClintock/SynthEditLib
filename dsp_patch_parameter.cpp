

#include <float.h>
#include <math.h>
#include "./dsp_patch_parameter.h"
#include "UPlug.h"
#include "PatchStorage.h"
#include "datatype_to_id.h"
#include "./modules/shared/xplatform.h"
#include "./IDspPatchManager.h"
#include "ug_midi_automator.h"
#include "./ug_container.h"
#include "midi_defs.h"
#include "tinyxml/tinyxml.h"
#include "plug_description.h"
#include "./iseshelldsp.h"
#include "modules/shared/voice_allocation_modes.h"
#include "my_msg_que_output_stream.h"
#include "BundleInfo.h"
#include "PresetReader.h"
#include "SeAudioMaster.h"


// see PatchParameter.cpp for other IDs.
SE_DECLARE_SERIAL2(20, dsp_patch_parameter, std::wstring, MetaData_filename );
SE_DECLARE_SERIAL2(21, dsp_patch_parameter, int,   MetaData_enum );
SE_DECLARE_SERIAL2(22, dsp_patch_parameter, float,   MetaData_float );
SE_DECLARE_SERIAL2(23, dsp_patch_parameter, bool,   MetaData_none );
SE_DECLARE_SERIAL2(24, dsp_patch_parameter, int,   MetaData_int );
SE_DECLARE_SERIAL2(28, dsp_patch_parameter, MpBlob,   MetaData_none );

typedef dsp_patch_parameter< std::wstring, MetaData_none> DspPatchParameterPlainString;
SE_DECLARE_SERIAL(30, DspPatchParameterPlainString) // plain strings without file-extension metadata.


// output parameter has changed value, send it to GUI
template<>
void dsp_patch_parameter<int, MetaData_enum>::CopyPlugValue( int voice, UPlug* p_plug)

{
	assert(p_plug->DataType == DT_ENUM);
	int val = p_plug->m_buffer.enum_ob;
	dsp_patch_parameter_base::SetValueRaw2( &val, sizeof(val), EffectivePatch(), voice );
}

template<>
void dsp_patch_parameter<float, MetaData_float>::CopyPlugValue( int voice, UPlug* p_plug)

{
	assert(p_plug->DataType == DT_FLOAT);
	dsp_patch_parameter_base::SetValueRaw2( &(p_plug->m_buffer.float_ob), sizeof(p_plug->m_buffer.float_ob), EffectivePatch(), voice );
}

template<>
void dsp_patch_parameter<std::wstring, MetaData_filename>::CopyPlugValue(int voice, UPlug* p_plug)
{
	assert(p_plug->DataType == DT_TEXT);
	std::wstring* val = p_plug->m_buffer.text_ptr;
	dsp_patch_parameter_base::SetValueRaw2((void*)val->data(), (int)(val->size() * sizeof(wchar_t)), EffectivePatch(), voice);
}

template<>
void dsp_patch_parameter<std::wstring, MetaData_none>::CopyPlugValue(int voice, UPlug* p_plug)
{
	assert(p_plug->DataType == DT_TEXT);
	std::wstring* val = p_plug->m_buffer.text_ptr;
	dsp_patch_parameter_base::SetValueRaw2((void*)val->data(), (int)(val->size() * sizeof(wchar_t)), EffectivePatch(), voice);
}

template<>
void dsp_patch_parameter<int, MetaData_int>::CopyPlugValue( int voice, UPlug* p_plug)

{
	assert(p_plug->DataType == DT_INT);
	dsp_patch_parameter_base::SetValueRaw2( &(p_plug->m_buffer.int_ob), sizeof(p_plug->m_buffer.int_ob), EffectivePatch(), voice );
}

template<>
void dsp_patch_parameter<bool, MetaData_none>::CopyPlugValue( int voice, UPlug* p_plug)

{
	assert( p_plug->DataType == DT_BOOL);
	dsp_patch_parameter_base::SetValueRaw2( &(p_plug->m_buffer.bool_ob), sizeof(p_plug->m_buffer.bool_ob), EffectivePatch(), voice );
}


template<>
void dsp_patch_parameter<MpBlob, MetaData_none>::CopyPlugValue( int voice, UPlug* p_plug)

{
	assert( p_plug->DataType == DT_BLOB && p_plug->io_variable );
	//	assert(!"needed?? can do better??");
	//TODO far better to pass raw event data direct to gui than to unpack to pin, then re-copy to que !!!
	MpBlob* blob = (MpBlob*) p_plug->io_variable;
	dsp_patch_parameter_base::SetValueRaw2( blob->getData(), blob->getSize(), EffectivePatch(), voice );
}


//float dsp_patch_parameter<std::wstring, MetaData_filename>::GetValueNormalised( int voiceId )
//{
//	return 0.0f;
//};
/*
// APPLE requires this in cpp, MS in header.
#if defined( __GNU_CPP__ )
// Specialized for strings.
template <>
void dsp_patch_parameter<wstring, MetaData_filename>::SetValue FromXml( const char* valueString, const char* end, int voice, int patch )
{
	std::string v(valueString, end - valueString);
	std::wstring value;
	MyTypeTraits<std::wstring>::parse( v.c_str(), value );
	const void* data = value.data();
	size_t size = sizeof(wchar_t) * value.length();
	patchMemory[voice]->SetValue( data, size, patch );
}
#endif
*/
// Text and Blob can't be normalized.
//template <>
//float dsp_patch_parameter<MpBlob, MetaData_none>::GetValueNormalised( int voiceId )
//{
//	return 0.0f;
//};

template <>
void dsp_patch_parameter<float, MetaData_float>::setMetadataRangeMinimum( double rangeMinimum )
{
	MetaData_float::setRangeMinimum( (float) rangeMinimum );
}
template <>
void dsp_patch_parameter<float, MetaData_float>::setMetadataRangeMaximum( double rangeMaximum )
{
	MetaData_float::setRangeMaximum( (float) rangeMaximum );
}

template <>
void dsp_patch_parameter<int, MetaData_int>::setMetadataRangeMinimum( double rangeMinimum )
{
	MetaData_int::setRangeMinimum( (int) rangeMinimum );
}
template <>
void dsp_patch_parameter<int, MetaData_int>::setMetadataRangeMaximum( double rangeMaximum )
{
	MetaData_int::setRangeMaximum( (int) rangeMaximum );
}

/////////////////////////////////////////////////////////////
dsp_patch_parameter_base::dsp_patch_parameter_base() :
	m_patch_mgr(0)
	,isDspOnlyParameter_(false)
	,m_controller_id(-1)
	,m_grabbed(false)
	,hostNeedsParameterUpdate(false)
	,isPolyphonicGate_(false)
	,hostControlId_(HC_NONE)
	,isPolyphonic_(false)
	,Sdk2BackwardCompatibilityFlag_(false)
	,voiceContainer_(0)
//	, voiceContainerHandle_(-1)
	,moduleHandle_(-1)
	,moduleParameterId_(0)
{
}

dsp_patch_parameter_base::~dsp_patch_parameter_base()
{
	for(int i = (int) patchMemory.size() - 1 ; i >= 0 ; i-- )
	{
		delete patchMemory[i];
	}
}

void dsp_patch_parameter_base::setPatchMgr(IDspPatchManager* p_patch_mgr)
{
	m_patch_mgr = p_patch_mgr;
}

void dsp_patch_parameter_base::OnUiMsg(int p_msg_id, my_input_stream& p_stream)
{
	switch( p_msg_id )
	{
	case code_to_long('p','p','c',0): // "ppc" Patch parameter change. Either Output parameter or MIDI automation
	{
		bool due_to_program_change;
		p_stream >> due_to_program_change;
		
//		int patch;
//		p_stream >> patch;

		int voice = 0;
		if( isPolyphonic() )
		{
			p_stream >> voice;
		}

		// Processor has only one patch, regardless of what patch the UI is using.
		constexpr int processorPatch = 0;
		const auto changed = SerialiseValue( p_stream, voice, processorPatch);

		if(changed && EffectivePatch() == processorPatch)
		{
			OnValueChangedFromGUI( due_to_program_change, voice );
		}
//		_RPTN(_CRT_WARN, "DSP ppc %10s v=%d val=%f\n", debugName.c_str(), voice, GetValueNormalised() );
	}
	break;

	case code_to_long('C','C','I','D'): // "CCID" Controller ID change
	{
		int controller_id = {};
		p_stream >> controller_id;
		setAutomation(controller_id);
	}
	break;

	case code_to_long('C','C','S','X'): // "CCSX" Controller ID change
	{
		p_stream >> m_controller_sysex;
	}
	break;

	case code_to_long('m','e','t','a'): // "meta"
	{
		SerialiseMetaData( p_stream );
	}
	break;

	case code_to_long('G', 'R', 'A', 'B'): // "GRAB"
	{
		p_stream >> m_grabbed;
//		_RPT1(_CRT_WARN, "GRAB %d\n", (int)m_grab);
	}
	break;
	
	default:
		dsp_msg_target::OnUiMsg( p_msg_id, p_stream );
	};
}

void dsp_patch_parameter_base::outputMidiAutomation(bool p_due_to_program_change, int voiceId)
{
	// output MIDI automation from Patch-Automator MIDI out
	if( hasNormalized() )
	{
		// Send MIDI Controller.
		// OLD: Didn't work in oversamper when patch-manager in higher level, because OS has no automation_output_device. Instead use patch-manager's container.
		// ug_patch_automator_out* midi_automator_out = fromPin->UG->patch_control_container->automation_output_device;
		ug_patch_automator_out* midi_automator_out = m_patch_mgr->Container()->automation_output_device;

		if( midi_automator_out )
		{
			int automationVoice = voiceId;
			int patchmemVoice = voiceId;

			if( !isPolyphonic() )
			{
				// With a monophonic control (e.g. slider), the value is taken from V0, because the Slider is monophonic.
				// The voice-id sent is from the Controller-ID (e.g. Voice 30 Poly Pressure).
				automationVoice = UnifiedControllerId() & 0x7f;
				patchmemVoice = 0;
			}

			int* lastMidiValue = &( lastMidiValue_[patchmemVoice] );

			midi_automator_out->SendAutomation2(GetValueNormalised(patchmemVoice), automationVoice, UnifiedControllerId(), m_controller_sysex.c_str(), p_due_to_program_change, lastMidiValue);
		}
	}
}

void dsp_patch_parameter_base::SendValue( timestamp_t unadjusted_timestamp, int voiceId )
{
	Voice* voice = nullptr;

	if (isPolyphonic())
	{
		auto voiceControlContainer = getVoiceContainer();
		if (!voiceControlContainer)
		{
			// unusual. poly *output* parameters *could* be set from GUI (but should not), and we end up here. Even though the parameter has no output pins anyhow.
			// behaviour noticed from "PT-Scope"
			return;
		}

		if (isPolyphonicGate())
		{
			auto myContainer = m_patch_mgr->Container();
			auto timestamp = voiceControlContainer->CalculateOversampledTimestamp(myContainer, unadjusted_timestamp);

			assert(typeIsVariableSize() == false); // must be DT_FLOAT
			int32_t size = 0;
			auto data = SerialiseForEvent(voiceId, size);
			if (0.0f < *(float*)data)
			{
				voiceControlContainer->VoiceAllocationNoteOn(timestamp, /*channel,*/ voiceId);

				// Voice allocation will send all voice-parameters including gate. No need to double-up.
				return;
			}
			else
			{
				// handle note-priority etc (whether physical voice present or not).
				voiceControlContainer->VoiceAllocationNoteOff(timestamp, /*channel,*/ voiceId);
			}
		}

		voice = voiceControlContainer->GetVoice(static_cast<short>(voiceId));
	}

	SendValuePt2( unadjusted_timestamp, voice);
}

/* !!! this is wrong. Poly controllers should go to all active voices with the matching voice-ID. Not a single physical voice.
 Only gate, trigger voice-active should go to physical voices?
*/
void dsp_patch_parameter_base::SendValuePt2( timestamp_t unadjusted_timestamp, Voice* voice, bool isInitialUpdate)
{
	int32_t size = 0;
	const int voiceId = voice ? voice->NoteNum : 0;
	auto data = SerialiseForEvent(voiceId, size);

	if( AffectsVoiceAllocation(getHostControlId()) && getVoiceContainer() ) // Voice container will be null if container muted.
	{
		getVoiceContainer()->OnVoiceControlChanged(getHostControlId(), size, data);
	}

	const RawView raw(data, size);

	if( isPolyphonic() )
	{
		if (voice)
		{
			SendValuePoly(unadjusted_timestamp, voice->m_voice_number, raw, isInitialUpdate);
		}
	}
	else // Monophonic.
	{
		const auto myContainer = m_patch_mgr->Container();
		for( auto fromPin : outputPins_ )
		{
			timestamp_t timestamp = fromPin->UG->ParentContainer()->CalculateOversampledTimestamp( myContainer, unadjusted_timestamp );
			if (fromPin->GetFlag(PF_PARAMETER_1_BLOCK_LATENCY) && !isInitialUpdate)
			{
				timestamp += fromPin->UG->AudioMaster()->BlockSize();
			}
			fromPin->Transmit( timestamp, size, data );
		}
	}

	if (!isInitialUpdate)
	{
		// update the preset state manager
		shellDsp_->onSetParameter(Handle(), gmpi::FieldType::MP_FT_VALUE, raw, voiceId);
	}
}

void dsp_patch_parameter_base::SendValuePoly(timestamp_t unadjusted_timestamp, int physicalVoiceNumber, RawView value, bool isInitialUpdate)
{
	const auto myContainer = m_patch_mgr->Container();

	for (auto fromPin : outputPins_)
	{
		timestamp_t timestamp = fromPin->UG->ParentContainer()->CalculateOversampledTimestamp(myContainer, unadjusted_timestamp);

		if (fromPin->GetFlag(PF_PARAMETER_1_BLOCK_LATENCY) && !isInitialUpdate)
		{
			timestamp += fromPin->UG->AudioMaster()->BlockSize();
		}

		fromPin->TransmitPolyphonic(timestamp, physicalVoiceNumber, static_cast<int32_t>(value.size()), value.data());
	}
}

// for init by GUI.
bool dsp_patch_parameter_base::SetValueRaw2(const void* data, int size, int patch, int voice)
{
	assert( voice >=0 && voice < (int) patchMemory.size() );
	assert( patch >=0 && patch < 128 );
	return patchMemory[voice]->SetValue( data, size, patch );
}

int dsp_patch_parameter_base::GetValueRaw(const void*& returnData, int patch, int voice)
{
	assert(voice >= 0 && voice < (int)patchMemory.size());
	assert(patch >= 0 && patch < 128);

	returnData = patchMemory[voice]->GetValue(patch);
	return patchMemory[voice]->GetValueSize(patch);
}

RawView dsp_patch_parameter_base::GetValueRaw2(int patch, int voice)
{
	assert(voice >= 0 && voice < (int)patchMemory.size());
	assert(patch >= 0 && patch < 128);

	return patchMemory[voice]->GetValueRaw(patch);
}

int dsp_patch_parameter_base::queryQueMessageLength( int availableBytes )
{
	int messageLength = sizeof(hostNeedsParameterUpdate) + sizeof(int); // tailing -1

	int voice = 0;

	for(auto it = patchMemory.begin() ; it != patchMemory.end() ; ++it )
	{
		PatchStorageBase* voiceMemory = *it;

		for( int patch = voiceMemory->getPatchCount() - 1 ; patch >= 0 ; --patch )
		{
			if( voiceMemory->isDirty( patch ) )
			{
				int voiceDataSize = sizeof(int);// *2;

				if( typeIsVariableSize() )
				{
					voiceDataSize += sizeof(int);
				}

				voiceDataSize += patchMemory[voice]->GetValueSize(patch);

				// If we have enough capacity we will send this voice's data.
				if( availableBytes >= messageLength + voiceDataSize )
				{
					messageLength += voiceDataSize;
				}
				else
				{
					// if no more capacity, stop here.
					return messageLength;
				}
			}
		}

		++voice;
	}

	return messageLength;
}

void dsp_patch_parameter_base::getQueMessage( my_output_stream& outStream, int messageLength )
{
	// Write standard header. This not included in 'messageLength'.
	outStream << Handle();

	outStream << id_to_long("ppc");
	outStream << messageLength;
	// END of standard header, remainder is payload.

	outStream << !hostNeedsParameterUpdate;

	int bytesSent = sizeof(hostNeedsParameterUpdate) + sizeof(int); // '-1' marks end of message.

	// perhaps better is patch mem was one-dimensional, either 128 voices OR 128 patches never both.
	// scaning 128 X 128 would not be efficient.
	int voice = 0;
	constexpr int patch = 0;
	for( auto it = patchMemory.begin() ; it != patchMemory.end() ; ++it )
	{
		PatchStorageBase* voiceMemory = *it;

		//for( int patch = voiceMemory->getPatchCount() - 1 ; patch >= 0 ; --patch )
		{
			if( voiceMemory->isDirty( patch ) )
			{
				int voiceDataSize = sizeof(int);// *2;
				if( typeIsVariableSize() )
				{
					voiceDataSize += sizeof(int);
				}
				voiceDataSize += patchMemory[voice]->GetValueSize(patch);

				// estimate que use. If much data sent, defer remainder till next call.
				if( messageLength >= bytesSent + voiceDataSize )
				{
					outStream << voice;
					//outStream << patch;
					SerialiseValue( outStream, voice, patch );
					voiceMemory->setDirty( patch, false );

					bytesSent += voiceDataSize;
				}
				else
				{
					// No more capacity, stop here. More next time.
					m_patch_mgr->Container()->AudioMaster()->getShell()->RequestQue( this, Sdk2BackwardCompatibilityFlag_ );
					outStream << (int) -1; // indicate end of data.
					return;
				}
			}
		}

		++voice;
	}

	outStream << (int) -1; // indicate end of data.
	hostNeedsParameterUpdate = false;
}

void dsp_patch_parameter_base::UpdateUI(bool due_to_vst_automation_from_dsp_thread, int voice)
{
	// some objects exist only on DSP side.
	if( isDspOnlyParameter_ )
		return;

	/*
	#if defined( _DEBUG )
	// testing.
	int* temp = (int*) patchMemory[voice]->GetValue();
	_RPT2(_CRT_WARN, "dsp_patch_parameter_base::UpdateUI this=%x, val=%d\n", this, *temp );
	#endif
	*/
	if( !due_to_vst_automation_from_dsp_thread )
	{
		hostNeedsParameterUpdate = true;
	}

	patchMemory[voice]->setDirty( EffectivePatch(), true );
	// Request use of GUI que.  Que will callback getQueMessage() when que is free.
	m_patch_mgr->Container()->AudioMaster()->getShell()->RequestQue( this, Sdk2BackwardCompatibilityFlag_ );
}

bool dsp_patch_parameter_base::isTiming()
{
	return m_controller_id >= (ControllerType::BPM << 24) && m_controller_id <= (ControllerType::SongPosition << 24);
}

void dsp_patch_parameter_base::Initialize( class TiXmlElement* xml )
{
	xml->QueryIntAttribute("Module", &moduleHandle_);
	xml->QueryIntAttribute("ModuleParamId", &moduleParameterId_);
	
//	xml->QueryIntAttribute("ContainerHandle", &voiceContainerHandle_);

// moved. #if defined( _DEBUG )
//	debugName = FixNullCharPtr(xml->Attribute("DebugName"));
//#endif

	xml->QueryIntAttribute("Index", &WavesParameterIndex );

	xml->QueryBoolAttribute("persistant", &stateful_);
	xml->QueryBoolAttribute("ignoreProgramChange", &ignorePc);
	
	double rangeMinimum, rangeMaximum;
	std::string textMetadata;

	int r = xml->QueryDoubleAttribute( "RangeMinimum", &rangeMinimum );
	if( r == TIXML_SUCCESS )
	{
		setMetadataRangeMinimum( rangeMinimum );
	}
	r = xml->QueryDoubleAttribute( "RangeMaximum", &rangeMaximum );
	if( r == TIXML_SUCCESS )
	{
		setMetadataRangeMaximum( rangeMaximum );
	}
	r = xml->QueryStringAttribute( "MetaData", &textMetadata );
	if( r == TIXML_SUCCESS )
	{
		setMetadataTextMetadata( Utf8ToWstring(textMetadata.c_str()) );
	}

	int32_t v = -1;
	v = -1;
	xml->QueryIntAttribute("Handle", &v);
	SetHandle( v );

	v = 0;
	xml->QueryIntAttribute("isPolyphonic", &v);
	setPolyphonic( v != 0 );
	v = 0;
	xml->QueryIntAttribute("isPolyphonicGate", &v);
	setPolyphonicGate( v != 0 );
	v = 0;
	xml->QueryIntAttribute("Sdk2BackwardCompatibility", &v);
	setSdk2BackwardCompatibility( v != 0 );

	v = -1;
	xml->QueryIntAttribute("HostControl", &v);
	setHostControlId( (HostControls) v );

	int voiceCount = 1;
	xml->QueryIntAttribute("VoiceCount", &voiceCount);
	int patchCount = 128;
	xml->QueryIntAttribute("PatchCount", &patchCount);

	v = -1;
	xml->QueryIntAttribute("MIDI", &v);
	setAutomation( v );
	setAutomationSysex( Utf8ToWstring( xml->Attribute("MIDI_SYSEX") ) );

	assert( voiceCount == 128 || voiceCount == 1 );
	createPatchMemory( /*patchCount,*/ voiceCount );

	auto patch_xml = xml->FirstChildElement();
	assert(patch_xml == nullptr || strcmp(patch_xml->Value(), "patch-list") == 0);
	if (patch_xml)
	{
		ParseXmlPreset2(
			xml,
			[this](int voiceId, const char* xmlvalue)
			{
				// plugins have only one preset on the DSP at a time. Editor has many.
				const int preset = 0;
				SetValueFromXml(xmlvalue, voiceId, preset);
			}
		);
	}
	else
	{
		if (hostControlId_ == HC_VOICE_PITCH)
		{
			// Initialise pitch to western default tuning.
			const int patch = 0;
			assert(patchMemory.size() == 128);

			const float middleA = 69.0f;
			const float notesPerOctave = 12.0f;
			for (int voice = 0; voice < 128; ++voice)
			{
				assert( 1 == patchMemory[voice]->getPatchCount());

				float pitch = 5.0f + ((static_cast<float>(voice) - middleA) / notesPerOctave);

				patchMemory[voice]->SetValue(RawData3(pitch), RawSize(pitch), patch);
			}
		}
		else
		{
			InitializePatchMemory(L""); // zero patch memory to prevent random garbage in host controls.
		}
	}
}

void dsp_patch_parameter_base::SerialiseValue(my_output_stream& strm, int voice, int patch )
{
	int size = patchMemory[voice]->GetValueSize(patch);

	if( typeIsVariableSize() )
	{
		strm << size;
	}

	auto data = patchMemory[voice]->GetValue( patch );
	strm.Write(data, size);
}

bool dsp_patch_parameter_base::SerialiseValue( my_input_stream& p_stream, int voice, int patch )
{
	assert( voice >= 0 && voice < 128 );
	assert( patch >= 0 && patch < 128 );
	return patchMemory[voice]->SetValue( p_stream, patch );
}

const void* dsp_patch_parameter_base::SerialiseForEvent(int voice, int& size)
{
	int patch = EffectivePatch();
	size = patchMemory[voice]->GetValueSize(patch);
	return patchMemory[voice]->GetValue(patch);
}

// Pass pin from either PP Setter, or PP watcher.
void dsp_patch_parameter_base::ConnectPin2( UPlug* pin )
{
	if (pin->Direction == DR_OUT) // No action needed on output parameters.
	{
		outputPins_.push_back(pin);
	}
}

void dsp_patch_parameter_base::RegisterHandle( int handle )
{
	ISeAudioMaster* am = m_patch_mgr->Container()->AudioMaster();
	am->RegisterDspMsgHandle( this, handle );
}

void dsp_patch_parameter_base::setAutomation(int controller_id, bool notifyUI)
{
	m_controller_id = controller_id;
	m_patch_mgr->vst_setAutomationId(this, m_controller_id);

	if(notifyUI)
	{
		// send new controller ID to UI.
		my_msg_que_output_stream strm( m_patch_mgr->Container()->AudioMaster()->getShell()->MessageQueToGui(), Handle(), "lern");
		strm << (int) sizeof(m_controller_id); // message length.
		strm << m_controller_id;

		strm.Send();
	}
}

void dsp_patch_parameter_base::UpdateOutputParameter(int voiceId, UPlug* p_plug)
{
	assert(p_plug->Direction == DR_IN);

	// fix for Scope3 polydetect (monophonic parameter on polyphonic module).
	// TODO!!! should poly mono param on poly object be allowed?? !!
	if( !isPolyphonic() )
	{
		voiceId = 0;
	}

	CopyPlugValue( voiceId, p_plug );
	UpdateUI( false, voiceId );

	outputMidiAutomation(false, voiceId);

	// This does seem to double up on dsp_patch_parameter_base::SendValuePt2(), but not always (AI Master)
	const auto raw = GetValueRaw2(0, voiceId);
	shellDsp_->onSetParameter(Handle(), gmpi::FieldType::MP_FT_VALUE, raw, voiceId);
}

void dsp_patch_parameter_base::vst_automate2(timestamp_t timestamp, int voice, const void* data, int size, [[maybe_unused]] int32_t flags)
{
	const int patch = 0;
	int patchMemoryVoice = isPolyphonic() ? voice : 0; // For monophonic parameters, we always access voice zero.

	assert(patchMemoryVoice >= 0 && patchMemoryVoice < (int)patchMemory.size());

	if( patchMemory[patchMemoryVoice]->SetValue(data, size, patch) )
	{
		// In VST3, GUI is notified of automation by Host independantly. Don't double-up.
		// Exception is MIDI Messages internally mapped to Parameter.
		// (With SE.exe we must always notify GUI).
//#if defined( SE_TARGET_PLU GIN )
		if( flags & (kIsMidiMappedAutomation | kMustUpdateUi) )
//#endif
		{
			UpdateUI(false, patchMemoryVoice);
		}

#if 0 // disabled because: I needed to get rid of macro, and outputting MIDI CC is propably not needed in a plugin these days.
#if defined( SE_TARGET_PLU GIN )
		// In SE we don't re-transmit MIDI when parameter changed in response to MIDI. Only when User moves knob.
		// There is no way of distinguishing in VST3 processor if change came from user, or from automation.
		// So we send MIDI controllers regardless. MIDI output is kind of pointless in VST3 anyhow.
		outputMidiAutomation(false, voice);
#endif
#endif

		OnValueChanged(voice, timestamp);
	}
}

void dsp_patch_parameter_base::OnValueChanged(int voiceId, timestamp_t time, bool initialUpdate)
{
	if (time < 0) // means "don't have specific timestamp". Send at start of next block.
	{
		time = m_patch_mgr->Container()->AudioMaster()->NextGlobalStartClock();
	}

	SendValue(time, voiceId);

	switch (hostControlId_)
	{
	case HC_MPE_MODE:
	{
		// see also ug_container::PostBuildStuff()
		const int patch = EffectivePatch();
		const auto mpeMode = *(int32_t*)patchMemory[voiceId]->GetValue(patch);
		m_patch_mgr->Container()->AudioMaster2()->setMpeMode(mpeMode);
	}
	break;

	default:
		break;
	};

	if (requiresAsyncRestart() && !initialUpdate)
	{
		m_patch_mgr->Container()->AudioMaster()->getShell()->DoAsyncRestart();
	}
}

// send new value to destination plug and send MIDI automation. UI generated (no timestamp).
void dsp_patch_parameter_base::OnValueChangedFromGUI(bool p_due_to_program_change, int voiceId, bool initialUpdate)
{
//	_RPT1(_CRT_WARN, "                     PRESETS-DSP: P%d OnValueChangedFromGUI\n", m_handle);

	outputMidiAutomation(p_due_to_program_change, voiceId);

	OnValueChanged(voiceId, -1, initialUpdate);
}

void dsp_patch_parameter_base::SetGrabbed(bool pGrabbed)
{
	m_grabbed = pGrabbed;
}

// used when adding a voice mute, automation_receiver etc.
void dsp_patch_parameter_base::setUpFromDsp( parameter_description* parameterDescription, InterfaceObject* pinDescription )
{
	// adapted from serialise().
//	m_ug_handle = -1; // indicates DSP-only    plug->UG->Handle();
	isDspOnlyParameter_ = true;
	m_controller_id = parameterDescription->automation;
	m_patch_mgr->vst_setAutomationId(this, m_controller_id);
	//m_controller_sysex;
	isPolyphonic_ = pinDescription->isPolyphonic(); //true;
	isPolyphonicGate_ = pinDescription->isPolyphonicGate();
	int patchCount = 1; //1 28;

	if( (parameterDescription->flags & IO_IGNORE_PATCH_CHANGE) != 0 )
		patchCount = 1;

	int voiceCount = 1;

	if( isPolyphonic_ )
		voiceCount = 128; // use constant POLYPHONIC_ARRAY_SIZE!!!?

	assert( voiceCount == 1 || patchCount == 1 ); // Not usual to need both polyphonic AND 128 patches!!!
	createPatchMemory( /*patchCount,*/ voiceCount );
	const int ccVirtualVoiceId = ControllerType::VirtualVoiceId << 24;

	if( m_controller_id == ccVirtualVoiceId )
	{
		// special case: set virtual voice IDs
		const int size = sizeof(int);
		const int patch = 0; // assume only one patch.

		for(int voice = 0 ; voice < 128 ; ++voice )
		{
			void* data = &voice;
			patchMemory[voice]->SetValue( data, size, patch );
		}
	}
	else
	{
		InitializePatchMemory( parameterDescription->defaultValue.c_str() );
	}
}

void dsp_patch_parameter_base::createPatchMemory(int voiceCount)
{
const int patchCount = 1;

	assert(voiceCount > 0);
	patchMemory.resize(voiceCount);
	lastMidiValue_.assign(128, INT_MAX);

	{
		if (typeIsVariableSize())
		{
			for (int i = 0; i < voiceCount; i++)
			{
				patchMemory[i] = new PatchStorageVariableSize(patchCount);
			}
		}
		else
		{
			int itemSize = typeSize();

			for (int i = 0; i < voiceCount; i++)
			{
				patchMemory[i] = new PatchStorageFixedSize(patchCount, itemSize);
			}
		}
	}
}


// PatchManager.cpp : implementation file
//
/*
	with GIMPI, patch management will be handled by host.
	with VST, plugin needs to manage it.
	This class abstracts the Patch Managment.
*/
#include <algorithm>
#include <assert.h>
#include <iomanip>
#include "PatchParameter.h"
#include "PatchManager.h"
#include "conversion.h"
#include "datatype_to_id.h"
#include "Application.h"
#include "./PatchParameter_host_generated.h"
#include "midi_defs.h"
#include "module_info.h"
#include "SuspendDSP.h"
#include "Notify_msg.h"
#include "tinyxml/tinyxml.h"
#include "Base64.h"
#include "PresetReader.h"

// for backward compat
#include "CContainer.h"

// debugging
#include "SynthEditDocBase.h"
#include "Hosting/message_queues.h"

using namespace gmpi::hosting;
using namespace std;

// how many patches
#define PP_ARRAY_SIZE 128

#define MIDI_LEARN_MENU_ITEMS L"Learn=1, UnLearn, Edit..."
#define VOICES_COUNT 128

std::map< int, CPatchManager::paramUpdate2 > CPatchManager::parameterUpgrades;


// !! or simply use class name as id, save having to work out next biggest? (bigger to serialize?, or use index?)
//SE_DECLARE_SERIAL(20, dsp_patch_parameter_text );
//SE_DECLARE_SERIAL(21, dsp_patch_parameter_enum );
SE_DECLARE_SERIAL2(DT_STRING_UTF8, PatchParameter, std::string, MetaData_filename8 );
SE_DECLARE_SERIAL2(DT_TEXT, PatchParameter, std::wstring, MetaData_filename );
//SE_DECLARE_SERIAL2(DT_ENUM, PatchParameter, short,   MetaData_enum );
SE_DECLARE_SERIAL2(DT_ENUM, PatchParameter, int,   MetaData_enum );
SE_DECLARE_SERIAL2(DT_FLOAT,PatchParameter, float,   MetaData_float );
SE_DECLARE_SERIAL2(DT_BOOL,PatchParameter, bool,	 MetaData_none );
SE_DECLARE_SERIAL2(DT_INT,PatchParameter, int,		 MetaData_int );
SE_DECLARE_SERIAL2(DT_BLOB,PatchParameter, MpBlob,	 MetaData_none );

// see dsp_patch_parameter.cpp for 20->24, 28

SE_DECLARE_SERIAL( 25, PatchParameter_host_Program );
SE_DECLARE_SERIAL( 26, PatchParameter_host_ProgramName );
SE_DECLARE_SERIAL( 27, PatchParameter_host_ProgramNamesList );
typedef PatchParameter< std::wstring, MetaData_none> PatchParameterPlainString;
SE_DECLARE_SERIAL( 29, PatchParameterPlainString) // plain strings without file-extension metadata.

PatchParameter_base::PatchParameter_base() :
	m_automation(ControllerType::None)
{
	initStates();
}

PatchParameter_base::PatchParameter_base( CUG* p_module, parameter_description& descriptor ) :
	isStateful( (descriptor.flags& IO_PARAMETER_PERSISTANT ) != 0 )  // except for host-generated parameters
	,isPrivate( (descriptor.flags& IO_PAR_PRIVATE ) != 0 )
	,m_automation( descriptor.automation )
	,m_short_name(descriptor.name)
	,module_(p_module)
	,moduleParameterId_(descriptor.id)
	,isPolyphonic_( (descriptor.flags& IO_PAR_POLYPHONIC ) != 0 )
	,isPolyphonicGate_( (descriptor.flags& IO_PAR_POLYPHONIC_GATE ) != 0 )
	,isFilenameWritable_( (descriptor.flags& IO_FILENAME_WRITABLE ) != 0 )
{
	initStates();
}

void PatchParameter_base::initStates()
{
	isStateful.addObserver([this]() {setDocModified(); });
	isPrivate.addObserver([this]() {setDocModified(); });
	m_ignoreProgramChange.addObserver([this]()
		{
			setDocModified();

			// DSP side needs updating (lazy way).
			if (m_patch_mgr) // no need during initialization.
			{
				SuspendDSP x(m_patch_mgr->Container()->Document()->Application());
			}

			onSetIgnoreProgramChange();
		});
}

void PatchParameter_base::setDocModified()
{
	if(getPatchManager()) // else crashes during initial serialise
		getPatchManager()->Container()->Document()->SetModified();
}

//void PatchParameter_base::setPresetModified()
//{
//	getPatchManager()->setModified();
//}
//

PatchParameter_base::~PatchParameter_base()
{
	clearPatchMemory();
}

void PatchParameter_base::clearPatchMemory()
{
	for( int i = (int)patchMemory.size() - 1 ; i >= 0 ; --i )
	{
		delete patchMemory[i];
	}

	patchMemory.resize(0);
}

void PatchParameter_base::InitializePatchMemory( bool setDefaults, int patchCount )
{
	int voiceCount;

	if( isPolyphonic() )
	{
		voiceCount = VOICES_COUNT;
	}
	else
	{
		voiceCount = 1;
	}

	clearPatchMemory();
	assert( patchMemory.size() == 0 ); // else mem leaks
	patchMemory.resize(voiceCount);

	if( patchCount == -1 )
	{
		patchCount = calcPatchMemorySize();
	}

	if( typeIsVariableSize() )
	{
		for(int i = 0 ; i < voiceCount ; i++ )
		{
			patchMemory[i] = new PatchStorageVariableSize(patchCount);
		}
	}
	else
	{
		int itemSize = (int)GetValue(FT_DEFAULT).size(); // default don't require looking up effectivepatch (crash during serialise)

		for(int i = 0 ; i < voiceCount ; i++ )
		{
			patchMemory[i] = new PatchStorageFixedSize(patchCount,itemSize);
		}
	}

	if( setDefaults )
	{
		// initialise default values
		const auto rawDefault = GetValue(FT_DEFAULT);

		for(int v = 0 ; v < voiceCount ; v++ )
		{
			for( int i = 0 ; i < patchCount; i++ )
			{
				patchMemory[v]->SetValue(rawDefault.data(), (int)rawDefault.size(), i );
			}
		}

//		free(data);
		//crashes when loading older file		UpdateGui();
	}
}

int PatchParameter_base::getPatchCount()
{
	return patchMemory[0]->getPatchCount();
}

void PatchParameter_base::UpdateDspControllerId()
{
	UpdateDsp(m_automation, "CCID");
}

void PatchParameter_base::UpdateDspSysex()
{
	UpdateDsp(m_automation_sysex, "CCSX");
}

bool PatchParameter_base::IsCopyTagged()
{
	// Host controls like oversampling-rate won't have module.
	return module() && module()->IsCopyTagged();
}

void PatchParameter_base::setParameterId(int pinId)
{
	moduleParameterId_ = pinId;
}

double PatchParameter_base::GetValueReal(enum ParameterFieldType FieldId, int voice, int patch )
{
	int datatype;
	GetDatatype(FieldId, &datatype);

	int Actualpatch = patch;
	if( ignoreProgramChange() )
	{
		Actualpatch = 0;
	}

	double value;

	switch(datatype)
	{
	case DT_DOUBLE:
	case DT_FLOAT:
	{
		const auto raw = GetValue( FieldId, voice, Actualpatch );

		if( raw.size() == sizeof(float) )
			value = (double) RawToValue<float>( raw.data(), raw.size() );
		else
			value = RawToValue<double>( raw.data(), raw.size() );
	}
	break;

	default:
		assert(false); // Not a floating point value.
		value = 0.0;
	};

	return value;
}

int PatchParameter_base::GetValueInt(enum ParameterFieldType FieldId, int voice, int patch )
{
	int datatype;
	GetDatatype(FieldId, &datatype);

	int Actualpatch = patch;
	if( ignoreProgramChange() )
	{
		Actualpatch = 0;
	}

	const auto raw = GetValue( FieldId, voice, Actualpatch );

	int value;

	switch(datatype)
	{
	case DT_ENUM:
	case DT_BOOL:
	case DT_INT:
	{
		if( raw.size() == sizeof(bool) )
			value = (int) RawToValue<bool>( raw.data(), raw.size() );
		else
			value = RawToValue<int>( raw.data(), raw.size() );
	}
	break;

	default:
		assert(false); // Not a integer value.
		value = 0;
	};

	return value;
}

wstring PatchParameter_base::GetValueString(enum ParameterFieldType FieldId, int voice , int patch )
{
	int datatype;
	GetDatatype(FieldId, &datatype);

	int Actualpatch = patch;
	if( ignoreProgramChange() )
	{
		Actualpatch = 0;
	}

	const auto raw = GetValue( FieldId, voice, Actualpatch );

	wstring value = RawToValue<std::wstring>(raw.data(), raw.size());

	return value;
}

void PatchParameter_base::ExportXml(TiXmlElement* XmlParent, ExportFormatType targetType/*, bool vstHasInternalPresets*/)
{
	assert(targetType != SAT_SYNTHEDIT_DOCUMENT);

	if (instansiateDsp()) // ignore host-generated parameters with no patch memory (derived from PatchParameter_host). else causes crash in initializeDsp()
	{
		const int voiceCount = (std::max)(1, (int)patchMemory.size()); // PatchParameter_host_ProgramName has no patchmemory.

		int startPatch = 0;
		int endPatch = 127;

		// DSP and plugins have only one patch
		if (targetType == SAT_SYNTHEDIT_DSP)
		{
			startPatch = endPatch = EffectivePatch();
		}
		else
		{
			// Plugins don't have presets stored on process side. Save all the work.
			if (ignoreProgramChange() || targetType == SAT_VST3 || targetType == SAT_VST3_PARAMETERS)
			{
				endPatch = 0;
			}
		}

		assert(startPatch == endPatch);

		assert(voiceCount == 128 || voiceCount == 1);
		TiXmlElement* parameter_xml = new TiXmlElement("Parameter");
		XmlParent->LinkEndChild(parameter_xml);

		if (targetType != SAT_VST3_PARAMETERS)
		{
			parameter_xml->SetAttribute("DataType", DspDataTypeID()); // note: this is the type of patchparameter to be serialised, not the parameter datatype.
		}

		int mydatatype;
		GetDatatype(FT_VALUE, &mydatatype);

		parameter_xml->SetAttribute( "Handle", Handle() );

		bool isStateful = is_stateful() || hostControlId_ == HC_PROGRAM_NAME; // program name *should* be a regular stateful parameter, but for legacy reasons it's not.;

		{
			int v;
			if (isPolyphonic())
			{
				parameter_xml->SetAttribute("isPolyphonic", 1);

				v = isPolyphonicGate();

				if (v != 0)
					parameter_xml->SetAttribute("isPolyphonicGate", v);
			}

			if (module())
			{
				// module is either the module that uses the parameter, or the container (oversampling HC), or the poly-container (voice HCs)
				parameter_xml->SetAttribute("Module", ModuleHandle());
			}
			
			if (hostControlId_ == HC_NONE)
			{
				auto mpid = ModuleParameterId();
				if( mpid != 0 )
					parameter_xml->SetAttribute("ModuleParamId", mpid);
			}
#if 0 // redundant becuase "ContainerHandle" should be same as "Module" (handle)
			else
			{
				// Host controls attached to a container, store the actual parameter handle (because the same host control can appear in many containers)
				if (AttachesToVoiceContainer(hostControlId_))
				{
					// parameter MUST have voice control container as it's module
					assert(dynamic_cast<CContainer*>(module_)->getVoiceControlContainer()->Handle() == module_->Handle());
					v = module_->Handle();

					if (v != 0)
					{
						// DSP expects this syntax. Not sure it couldn't be same as below?
						parameter_xml->SetAttribute("ContainerHandle", v);
					}
				}
			}
#endif
			if (Sdk2BackwardCompatibilityFlag_ != 0)
				parameter_xml->SetAttribute("Sdk2BackwardCompatibility", Sdk2BackwardCompatibilityFlag_);

			if (voiceCount != 1)
				parameter_xml->SetAttribute("VoiceCount", voiceCount);

			if (startPatch != endPatch)
				parameter_xml->SetAttribute("PatchCount", (endPatch - startPatch) );

			if (m_automation != -1)
			{
				parameter_xml->SetAttribute("MIDI", m_automation);

				if (!m_automation_sysex.empty())
					parameter_xml->SetAttribute("MIDI_SYSEX", WStringToUtf8(m_automation_sysex));
			}

			bool is_private = false;
			IsPrivate(&is_private);

#ifdef _DEBUG
			if(!is_private && GetVstParameterNumber() == -1)
			{
				auto name = GetName();
				if(name.empty() && module_)
				{
					name = module_->GetName();
				}
//				_RPT1(_CRT_WARN, "PatchParameter_base::ExportXml() Forcing parameter to PRIVATE (%S)\n", name.c_str());
			}
#endif

			// Some parameters like Oversampling control are mandated private.
			// They can be identified by have an index of -1. see also PatchParameter_base::doesExportToPlugin()
			// Waves plugins may also have negative indexes (e.g. -2001) to represent output params like Scopes
			is_private |= GetVstParameterNumber() < 0;

			// If it's not private, it needs an index. If it is private, it can have and index, but the index is irelevant.
			assert(is_private || (GetVstParameterNumber() >= 0));

			if(GetVstParameterNumber() != -1)
				parameter_xml->SetAttribute("Index", GetVstParameterNumber());

			if (hostControlId_ != HC_NONE)
			{
				parameter_xml->SetAttribute("HostControl", hostControlId_);
			}

			if (ignoreProgramChange())
				parameter_xml->SetAttribute("ignoreProgramChange", ignoreProgramChange());
			
			if (is_private)
			{
				parameter_xml->SetAttribute("Private", 1);
			}

			if (!isStateful)
			{
				parameter_xml->SetAttribute("persistant", 0);
			}

			if (!hint.empty())
			{
				parameter_xml->SetAttribute("hint", hint);
			}

			int datatype;
			GetDatatype(FT_VALUE, &datatype);

			if (targetType == SAT_VST3_PARAMETERS)
			{
				parameter_xml->SetAttribute("Name", WStringToUtf8(m_short_name));
				if (datatype != DT_FLOAT)
				{
					parameter_xml->SetAttribute("ValueType", datatype);
				}
			}

			int wavesParamType = 0; // float.
			{
				const bool exportMetadata = can_automate() && (targetType == SAT_VST3 || targetType == SAT_VST3_PARAMETERS || targetType == SAT_SYNTHEDIT_DSP);

				switch( datatype )
				{
				case DT_STRING_UTF8:
				case DT_TEXT:
				{
					if(exportMetadata)
					{
						std::wstring textMetaData = GetValueString(FT_FILE_EXTENSION);

						if( !textMetaData.empty() )
						{
							parameter_xml->SetAttribute("MetaData", WStringToUtf8(textMetaData).c_str());
						}
					}
				}
				break;

				case DT_INT:
				{
					wavesParamType = 1; // int.

					if (exportMetadata)
					{
						int metadatatype;
						GetDatatype(FT_ENUM_LIST, &metadatatype);
						// Can identify ENUM parameters by checking what type of metadata they have.
						if( metadatatype == DT_TEXT )
						{
							std::wstring textMetaData = GetValueString(FT_ENUM_LIST);

							if( !textMetaData.empty() )
							{
								parameter_xml->SetAttribute("MetaData", WStringToUtf8(textMetaData).c_str());
							}
						}
						else
						{
							int rangeMinimum = GetValueInt(FT_RANGE_LO);
							int rangeMaximum = GetValueInt(FT_RANGE_HI);
							// Metadata so normalised parameter updates to the DSP work correctly.
							if( rangeMinimum != 0 )
							{
								parameter_xml->SetAttribute("RangeMinimum", rangeMinimum);
							}
							if( rangeMaximum != 10 )
							{
								parameter_xml->SetAttribute("RangeMaximum", rangeMaximum);
							}
						}
					}
				}
				break;

				case DT_FLOAT:
				case DT_DOUBLE:
				{
					if (exportMetadata)
					{
						double rangeMinimum = GetValueReal(FT_RANGE_LO);
						double rangeMaximum = GetValueReal(FT_RANGE_HI);
						// Metadata so normalised parameter updates to the DSP work correctly.
						if( rangeMinimum != 0.0 )
						{
							parameter_xml->SetDoubleAttribute("RangeMinimum", rangeMinimum);
						}
						if( rangeMaximum != 10.0 )
						{
							parameter_xml->SetDoubleAttribute("RangeMaximum", rangeMaximum);
						}
					}
				}
				break;
				}
			}

			if (wavesParamType != 0 && targetType != SAT_VST3_PARAMETERS)
			{
				parameter_xml->SetAttribute("WavesParamType", wavesParamType);
			}
		}

		// don't work on VoiceActive if( instansiateDsp() ) // ignore host-generated parameters with no patch memory. Else generates screeds of unnesc XML.
		if ((isStateful || SAT_SYNTHEDIT_DSP == targetType) && !isPolyphonic()) // 'isPolyphonic' is hack to avoid swathes of data on scope and voiceactive.
		{
			auto list_xml = new TiXmlElement("patch-list");
			parameter_xml->LinkEndChild(list_xml);

			// Actual Patch data
			// will only be one preset.
			// might be many voices.
			const auto patch = startPatch;
			assert(startPatch == endPatch);
//			for (int patch = startPatch; patch <= endPatch; ++patch)
			{
				for (int voice = 0; voice < voiceCount; ++voice)
				{
					std::string prev_patch_value;
					int repeat_count = 1;
					TiXmlElement* patch_xml = {};

					auto text = ToXmlStringSafe(voice, patch);
					if (text.size() > maxXmlAttributeBytes)
					{
						// XML can't really handle HUGE data (like Wavetables). Hack to blank out those.
						text.clear(); // blanked.
					}

					if (prev_patch_value == text && patch_xml)
					{
						patch_xml->SetAttribute("repeat", ++repeat_count);
					}
					else
					{
						patch_xml = new TiXmlElement("s");
						list_xml->LinkEndChild(patch_xml);
						patch_xml->LinkEndChild(
							new TiXmlText(text)
						);

						prev_patch_value = text;
						repeat_count = 1;
					}
				}

				//if (voice > 0)
				//{
				//	list_xml->SetAttribute("Voice", voice);
				//}
			}
		}
	}
}

// Export SynthEdit project file
tinyxml2::XMLElement* PatchParameter_base::Export(tinyxml2::XMLElement* parameters_xml, ExportFormatType targetType)
{
#ifdef _DEBUG
	{ // diagnose a bug.
		switch (hostControlId_)
		{
		case HC_VOICE_AFTERTOUCH:			// L"Voice/Aftertouch";ControllerType::PolyAftertouch << 24;
		case HC_CHANNEL_PRESSURE:
		case HC_VOICE_PITCH:				// L"Voice/Pitch";ControllerType::Pitch << 24;
		case HC_VOICE_VELOCITY_KEY_ON:		// L"Voice/VelOn";ControllerType::VelocityOn << 24;
		case HC_VOICE_VELOCITY_KEY_OFF:		// L"Voice/VelOff";ControllerType::VelocityOff << 24;
		case HC_VOICE_TRIGGER:				// L"Voice/Trigger";ControllerType::Trigger << 24;
		case HC_VOICE_ACTIVE:				// L"Voice/Active";ControllerType::Active << 24;
		case HC_HOLD_PEDAL:					// L"Hold Pedal";(ControllerType::CC << 24) | 64;
		case HC_TIME_QUARTER_NOTE_POSITION:	// L"Time/SongPosition";ControllerType::SongPosition << 24;
		case HC_TIME_BPM:					// L"Time/BPM"; //ControllerType::BPM << 24;
		case HC_VOICE_GATE:					// L"Voice/Gate";ControllerType::Gate << 24;
		case HC_PITCH_BENDER:				// L"Bender";ControllerType::Bender << 24;
		case HC_TIME_BAR_START:				// 
		case HC_VOICE_VOLUME:				// 
		case HC_VOICE_PAN:					// 
		case HC_VOICE_PITCH_BEND:
		case HC_VOICE_VIBRATO:				// 
		case HC_VOICE_EXPRESSION:			// 
		case HC_VOICE_BRIGHTNESS:			// 
		case HC_VOICE_USER_CONTROL0:		// 
		case HC_VOICE_USER_CONTROL1:		// 
		case HC_VOICE_USER_CONTROL2:		// 
		case HC_VOICE_PORTAMENTO_ENABLE:
		case HC_GLIDE_START_PITCH:

			// fix screwup in early prototypes (Jan 2019). Prevent saving in future.
		case HC_PROGRAM_CATEGORIES_LIST:
			assert(!(bool) isStateful); // try to figure out how this can happen.
			break;
                
        default:
            break;
		};
	}
#endif
	
	assert(targetType == SAT_SYNTHEDIT_DOCUMENT); // preset format is DIFFERENT and incompatible in project files
	assert(hostControlId_ != HC_NONE || ModuleHandle() != 0);
	assert(module_ || hostControlId_ != HC_NONE);

	if(!is_saved_in_project() || (CDocOb::serialise_copy_mode && !IsCopyTagged()))
	{
		return {};
	}

	auto parameter_xml = parameters_xml->GetDocument()->NewElement("param");
	parameters_xml->LinkEndChild(parameter_xml);

	assert(ClassTypeId() > -1);
	parameter_xml->SetAttribute("type", ClassTypeId());

	// simple fields
	{
		Archive2 ar{ true , parameter_xml, targetType };
		Serialize2(ar);
	}

	if (ModuleHandle() != -1)
	{
		parameter_xml->SetAttribute("module", ModuleHandle());
	}

	if(hostControlId_ == HC_NONE)
	{
		if(ModuleParameterId() != -1)
		{
			parameter_xml->SetAttribute("moduleParamId", ModuleParameterId());
		}
	}
	else
	{
		parameter_xml->SetAttribute("hostControl", (int) hostControlId_);

#if 0 // "ContainerHandle" seems nesc only when serialising to DSP?
		// Host controls attached to a container, store the actual parameter handle (because the same host control can appear in many containers)
		if(AttachesToPolyphonyContainer(hostControlId_))
		{
			// shared host controls attach to container.
			auto container = dynamic_cast<CContainer*>(module_);
			if(!container)
			{
				// Private poly parameters attach to module.
				container = getPatchManager()->Container();
			}

			auto moduleHandle = container->Handle();
			if(moduleHandle)
			{
				parameter_xml->SetAttribute("containerHandle", moduleHandle);
			}
		}
#endif
	}

	// don't work on VoiceActive if( instansiateDsp() ) // ignore host-generated parameters with no patch memory. Else generates screeds of unnesc XML.

/*
IMPORTANT:  this XML preset format is different than that used by plugins
preset values are grouped by voice/preset, so a repeated value represents multiple presets with same value (not multiple voices)
(it's the other way round in plugins)

*/
	if (is_stateful())
	{
		const int patchCount = calcPatchMemorySize();
		const int voiceCount = static_cast<int>(patchMemory.size());
		auto doc = parameters_xml->GetDocument();

		auto list_xml = doc->NewElement("patch-list");
		parameter_xml->LinkEndChild(list_xml);

		// Patch data
		for (int voice = 0; voice < voiceCount; ++voice)
		{
			std::string prev_patch_value;
			int repeat_count = 1;
			tinyxml2::XMLElement* patch_xml = {};

			for (int patch = 0; patch < patchCount; ++patch)
			{
				auto text = ToXmlStringSafe(voice, patch);
				if (text.size() > maxXmlAttributeBytes)
				{
					// XML can't really handle HUGE data (like Wavetables). Hack to blank out those.
					text.clear(); // blanked.
				}

				if (prev_patch_value == text && patch_xml)
				{
					patch_xml->SetAttribute("repeat", ++repeat_count);
				}
				else
				{
					patch_xml = doc->NewElement("s");
					list_xml->LinkEndChild(patch_xml);
					patch_xml->LinkEndChild(doc->NewText(text.c_str()));

					prev_patch_value = text;
					repeat_count = 1;
				}
			}

			if (voice > 0)
			{
				list_xml->SetAttribute("Voice", voice);
			}
		}
	}

	return parameter_xml;
}

void PatchParameter_base::Import(tinyxml2::XMLElement* parameter_xml, ExportFormatType targetType)
{
	assert(SAT_SYNTHEDIT_DOCUMENT == targetType); // uses .synthedit project specific patch format

	// simple fields
	Archive2 ar{ false , parameter_xml, targetType };
	Serialize2(ar);

	parameter_xml->QueryIntAttribute("moduleParamId", &moduleParameterId_);
	
	{
		int temp = HC_NONE;
		parameter_xml->QueryIntAttribute("hostControl", &temp);
		hostControlId_ = (HostControls) temp;
	}

	int datatype{};
	GetDatatype(FT_VALUE, &datatype);

	const int patchCount = m_ignoreProgramChange ? 1 : 128;
	InitializePatchMemory( !is_stateful(), patchCount ); // for non-statefull, set patch to datatype default value.

	if(is_stateful())
	{
		ParseXmlPreset(
			parameter_xml,
			[this, patchCount, datatype](int voiceId, int preset, const char* xmlvalue)
			{
				assert(voiceId >= 0 && voiceId < (int)patchMemory.size());

				if (preset < patchCount && voiceId >= 0 && voiceId < (int)patchMemory.size())
				{
					const auto raw = ParseToRaw(datatype, xmlvalue);
					patchMemory[voiceId]->SetValue(raw.data(), (int)raw.size(), preset);
				}
			}
		);
	}
}

bool PatchParameter_base::doesExportToPlugin(ExportFormatType targetType)
{
	// Identify scope parameters (first trace only at this point).
	int datatype;
	GetDatatype( FT_VALUE, &datatype );

	bool isScope = isPolyphonic() && datatype == DT_BLOB && moduleParameterId_ == 0;

	bool dont_export = hostControlId_ == HC_OVERSAMPLING_RATE || hostControlId_ == HC_OVERSAMPLING_FILTER || hostControlId_ == HC_VOICE_ALLOCATION_MODE;

	bool lIsPrivate;
	IsPrivate(&lIsPrivate);

	return (isScope || ( is_stateful() && instansiateDsp() && !isPolyphonic() && !dont_export )) && !lIsPrivate;
}

int PatchParameter_base::WavesGetPrecision(float WvMin, float WvMax, float* returnResolution )
{
	int stringPrecision = -1; // Auto.
	float resolutionf = 0.1f;
	int datatype;
	GetDatatype(FT_VALUE, &datatype);

	if( datatype == DT_FLOAT || datatype == DT_DOUBLE )
	{
		stringPrecision = 0;
		// Calculate resolution as /1000 the range.
		double min = WvMin; // GetValueReal(FT_RANGE_LO);
		double max = WvMax; // GetValueReal(FT_RANGE_HI);
		double r = fabs(max-min);
		const int maxStringPrecision = 3; // 3 decimal  places max.

		// range of 25+	     : precision = 0.1
		// range of 2.5 - 25 : precision = 0.01
		// range of 0.25 - 2.5 : precision = 0.001
		while( r <= 25.0f && stringPrecision < maxStringPrecision)
		{
			r *= 10.0f;
			resolutionf *= 0.1f;
			stringPrecision++;
		}
	}

	if( returnResolution )
		*returnResolution = resolutionf;

	return stringPrecision;
}

bool PatchParameter_base::WavesIsParamNormalized()
{
	if( module() != nullptr )
	{
		if(module()->getType()->UniqueId() == L"Slider" )
		{
			IPlug* p = module()->GetPlug(L"Appearance");
			if( p ) // allow for older file version
			{
				const auto disp_type = StringToInt(p->GetDefault());
				return disp_type == 5 || disp_type == 9; // toggle buttons.
			}
		}
	}
	return false;
}

void PatchParameter_base::UpdateDspValue(int patch, int voice)
{
	auto pendingQueueClients = m_patch_mgr->Container()->Document()->Application()->PendingDspClients();
	if (!pendingQueueClients)
		return; // Processor not running.

	patchMemory[voice]->setDirty(EffectivePatch(), true);
	pendingQueueClients->AddWaiter(this);
}

int PatchParameter_base::queryQueMessageLength(int availableBytes)
{
	// we can only send current patch, even if others are dirty. DSP only has one patch.
	const auto patch = EffectivePatch();

	const int32_t perValueSize =
		  (typeIsVariableSize() ? sizeof(int32_t) : 0)  // value size
		+ (isPolyphonic()       ? sizeof(int32_t) : 0); // voice number

	int32_t totalBytes{};
	for (int voice = 0; voice < patchMemory.size(); voice++)
	{
		auto& voiceMemory = patchMemory[voice];
		if (voiceMemory->isDirty(patch))
		{
			const auto valueSize = voiceMemory->GetValueSize(patch);
			totalBytes += perValueSize + valueSize;
		}
	}

	if(isPolyphonic())
		totalBytes += sizeof(int32_t); //  terminator, -1

	return totalBytes;
}

void PatchParameter_base::getQueMessage(gmpi::hosting::my_output_stream& outStream, int messageLength)
{
	// Write standard header. This not included in 'messageLength'.
	outStream << Handle();
	outStream << gmpi::hosting::id_to_long("ppc");
	outStream << messageLength;
	// END of standard header, remainder is payload.

	// we can only send current patch, even if others are dirty. DSP only has one patch.
	const auto patch = EffectivePatch();

	int32_t totalBytes = isPolyphonic() ? sizeof(int32_t) : 0; // terminator

	const int32_t perValueSize =
		  (typeIsVariableSize() ? sizeof(int32_t) : 0)  // value size
		+ (isPolyphonic()       ? sizeof(int32_t) : 0); // voice number

	// perhaps better is patch mem was one-dimensional, either 128 voices OR 128 patches never both.
	// scaning 128 X 128 would not be efficient.
	for (int voice = 0; voice < patchMemory.size(); voice++)
	{
		auto& voiceMemory = patchMemory[voice];

		if (!voiceMemory->isDirty(patch))
			continue;

		const auto raw = GetValue(FT_VALUE, voice, patch);
		const int32_t rawSize = (int32_t)raw.size();
		const int32_t voiceDataSize = perValueSize + rawSize;

		// estimate que use. If much data sent, defer remainder till next call.
		if (messageLength >= totalBytes + voiceDataSize)
		{
			if(isPolyphonic())
				outStream << voice;

			if (typeIsVariableSize())
				outStream << rawSize;

			outStream.Write(raw.data(), rawSize);

			voiceMemory->setDirty(patch, false);

			totalBytes += voiceDataSize;
		}
		else
		{
			// No more capacity, stop here. More next time.
			auto pendingQueueClients = m_patch_mgr->Container()->Document()->Application()->PendingDspClients();
			if(pendingQueueClients)
				pendingQueueClients->AddWaiter(this);

			break;
		}
	}

	if (isPolyphonic())
		outStream << (int32_t) -1; // indicate end of data.
}

IWriteableQue* PatchParameter_base::MessageQueToDsp()
{
	return m_patch_mgr->Container()->Document()->Application()->MessageQueToDspOrNull();
}

int32_t PatchParameter_base::GetDatatype(enum ParameterFieldType field, int* returnValue)
{
	// automation screen checks ints for FT_ENUM_LIST (may fail) assert(false && "TODO" );
	*returnValue = getFieldDatatype( field );
	return gmpi::MP_OK;
}

std::wstring PatchParameter_base::fullPath()
{
	std::wstring fullpath;
	CUG* m = module();
	if( m != nullptr )
	{
		fullpath = m->getPath();
		fullpath.append( L"/" );
	}

	fullpath.append( m_short_name );
	return fullpath;
	/*
		std::wstring containerName;

		if( m->Container() )
		{
			containerName = m->Container()->GetName() + (L"/");
		}
		std::wstring fullPath = containerName + m->GetName() + (L"/") + std::wstring( m_short_name.c_str());

		return ( fullPath );
	*/
}

// No memory allocation, just a view of the stored value (data + size together).
// For the cases that stage data in GetRaw2TemporaryStorage_, the view is valid only
// until the next GetValueImpl() call on this object.
RawView PatchParameter_base::GetValueImpl( ParameterFieldType field, int voice, int patch )
{
	if( patch == -1 )
		patch = EffectivePatch();

	switch(field)
	{
	case FT_VALUE:
		assert( voice >=0 && voice < (int) patchMemory.size() );
		return RawView( patchMemory[voice]->GetValue( patch ), patchMemory[voice]->GetValueSize( patch ) );

	case FT_LONG_NAME:
		GetRaw2TemporaryStorage_ = ToRaw4(fullPath());
		return RawView( GetRaw2TemporaryStorage_.data(), GetRaw2TemporaryStorage_.size() );

	case FT_SHORT_NAME:
		return RawView( m_short_name );

	case FT_HINT:
		return RawView( hint );

	case FT_MENU_ITEMS:
	{
		int dt;
		GetDatatype( FT_VALUE, &dt );

		const size_t size = (dt == DT_BLOB || dt == DT_TEXT) ? 0 : RawSize( MIDI_LEARN_MENU_ITEMS );
		return RawView( MIDI_LEARN_MENU_ITEMS, size );
	}

	case FT_MENU_SELECTION:
		GetRaw2TemporaryStorage_ = ToRaw4((int) -1);
		return RawView( GetRaw2TemporaryStorage_.data(), GetRaw2TemporaryStorage_.size() );

	case FT_IGNORE_PROGRAM_CHANGE:
		// m_ignoreProgramChange is a gmpi_forms::State<bool> (polymorphic - starts with a vtable
		// pointer). Read the inner bool via .get(), NOT &the-object, or we serialise the vtable
		// pointer's low byte instead of the flag (cf. FT_STATEFUL below).
		return RawView( &(m_ignoreProgramChange.get()), getFieldSize(field) );

	case FT_STATEFUL:
		return RawView( &(isStateful.get()), getFieldSize(field) );

	case FT_PRIVATE:
		// isPrivate is a gmpi_forms::State<bool> - take the address of the inner bool via .get(),
		// not of the State object (whose first bytes are a vtable pointer, low byte ~0x08). Copying
		// that garbage byte through UpgradeAll's field copy is what silently marked upgraded
		// PatchMemory parameters private, dropping them from the exported parameters.h.
		return RawView( &(isPrivate.get()), getFieldSize(field) );

	case FT_AUTOMATION:
		return RawView( &m_automation, getFieldSize(field) );

	case FT_AUTOMATION_SYSEX:
		return RawView( m_automation_sysex );

	case FT_GRAB:
		return RawView( &m_grabbed, getFieldSize(field) );

	case FT_NORMALIZED:
		GetRaw2TemporaryStorage_ = ToRaw4(getValueNormalised());
		return RawView( GetRaw2TemporaryStorage_.data(), GetRaw2TemporaryStorage_.size() );

	case FT_HOST_PARAMETER:
		GetRaw2TemporaryStorage_ = ToRaw4( (bool) (hostControlId_ != HC_NONE) );
		return RawView( GetRaw2TemporaryStorage_.data(), GetRaw2TemporaryStorage_.size() );

	case FT_VST_PARAMETER_INDEX:
		return RawView( &vstParameterNumber_, getFieldSize(field) );

	case FT_VST_DISPLAY_TYPE:
		return RawView( &vstDisplayType_, getFieldSize(field) );

	case FT_VST_DISPLAY_MIN:
		return RawView( &vstDisplayMin_, getFieldSize(field) );

	case FT_VST_DISPLAY_MAX:
		return RawView( &vstDisplayMax_, getFieldSize(field) );

	default:
		assert(false && "TODO" );
		return {};
	};
}

void PatchParameter_base::SetValueRaw(ParameterFieldType field, const void* data, int size, int voice, int patch )
{
	bool isStateFull = is_stateful();
	bool notifiedPatchManager = false;
	bool changed = true;

	switch(field)
	{
	case FT_VALUE:

		/*
				#if defined( _DEBUG )
				int dataType;
				GetDatatype(field, &dataType);
				if( dataType != DT_BLOB ) // ignore scope updates.
				{
					std::wstring valString = valueToString( dataType, data, size );
					_RPTW3(_CRT_WARN, L"GUI PARAM %s (V%d) = %s\n", m_short_name.c_str(), voice, valString.c_str() );
				}
				#endif
		*/
		if( patch == -1 )
			patch = EffectivePatch();

		assert( voice >=0 && voice < (int)patchMemory.size() );

		changed = patchMemory[voice]->SetValue(data, size, patch);
		if(changed)
		{
			//	_RPT1(_CRT_WARN, "%d bytes : B1.5\n", calculateHeapSize() );
			if( !m_value_being_set_from_dsp )
			{
				UpdateDspValue( patch, voice );
			}

			// Update Normalized.
			if( patch == EffectivePatch() )
			{
				UpdateGui( FT_NORMALIZED, voice );	// update views
				UpdateGui( FT_VALUE, voice );
				notifiedPatchManager = true;
			}

			//	_RPT1(_CRT_WARN, "%d bytes : B2\n", calculateHeapSize() );

			if (is_stateful())
				getPatchManager()->setModified(true);
		}

		break;

	case FT_SHORT_NAME:
		m_short_name = RawToValue<std::wstring>(data, size);
		UpdateGui( field, voice );
		notifiedPatchManager = true;
		break;

	case FT_HINT:
		hint = RawToValue<std::string>(data, size);
		UpdateGui(field, voice);
		notifiedPatchManager = true;
		break;

	case FT_MENU_ITEMS: // read only
		break;

	case FT_MENU_SELECTION:
		OnPopupParameterMenu( RawToValue<int>(data, size) );
		isStateFull = false;
		break;

	case FT_IGNORE_PROGRAM_CHANGE:
		// m_ignore_program_change = RawToValue<bool>(data, size);
		setIgnoreProgramChange( RawToValue<bool>(data, size) );
		UpdateGui(field);
		notifiedPatchManager = true;
		break;

	case FT_STATEFUL:
		isStateful = RawToValue<bool>(data, size);
		UpdateGui(field);
		notifiedPatchManager = true;
		break;
		
	case FT_PRIVATE:
		isPrivate = RawToValue<bool>(data, size);
		UpdateGui(field);
		notifiedPatchManager = true;
		break;

	case FT_AUTOMATION:
		m_automation = RawToValue<int>(data, size);
		UpdateDspControllerId();
		UpdateGui(field);
		notifiedPatchManager = true;
		break;

	case FT_AUTOMATION_SYSEX:
		m_automation_sysex = RawToValue<std::wstring>(data, size);
		UpdateDspSysex();

		break;

	case FT_GRAB:
		{
			isStateFull = false;

			bool oldVal = m_grabbed;
			m_grabbed = RawToValue<bool>(data, size);

			// May already be grabbed by MIDI automation. In which case no need to update GUI.
			if(oldVal != m_grabbed)
				UpdateGui(field);

			// Mouse overrides MIDI "grab".
			m_grabbed_by_MIDI_timer = false;

			UpdateDsp(m_grabbed, "GRAB");
		}
		break;

	case FT_NORMALIZED:
		// recursive. Calls back here with FT_VALUE. no need to set changed flag etc.
		isStateFull = false; // to prevent setting document changed flag.
		setValueNormalised( RawToValue<float>( data, size ), false );
		break;

	case FT_VST_PARAMETER_INDEX:
		vstParameterNumber_ = RawToValue<int>(data, size);
		UpdateGui(field);
		notifiedPatchManager = true;
		break;

	case FT_VST_DISPLAY_TYPE:
		vstDisplayType_ = RawToValue<int>(data, size);
		UpdateGui(field);
		notifiedPatchManager = true;
		break;

	case FT_VST_DISPLAY_MIN:
		vstDisplayMin_ = RawToValue<float>(data, size);
		UpdateGui(field);
		notifiedPatchManager = true;
		break;

	case FT_VST_DISPLAY_MAX:
		vstDisplayMax_ = RawToValue<float>(data, size);
		UpdateGui(field);
		notifiedPatchManager = true;
		break;

	default:
		assert(false && "TODO" );
	};

	// update views
	if(!notifiedPatchManager && (patch == -1 || patch == EffectivePatch()))
	{
		m_patch_mgr->OnParameterUpdate( this, field, voice, data, size );
	}

	if( isStateFull && changed)
	{
		getPatchManager()->Container()->Document()->SetModified();
	}
}

void PatchParameter_base::UpdateGui(ParameterFieldType paramType, int voice)
{
	const auto raw = GetValue(paramType, voice);
	const void* data = raw.data();
	const int size = (int)raw.size();
	m_patch_mgr->OnParameterUpdate( this, paramType, voice, data, size );

	// WPF way. Also used by VST2 wrapper controller object.
	for( auto watcher : watchers )
	{
		watcher->OnParamUpdateFromHost(Handle(), paramType, voice, data, size);
	}
}

bool PatchParameter_base::ignoreProgramChange()
{
	//	return m_ignore_program_change || module()->Container()->getIgnoreProgramChange();
	// module() may be nullptr during upgrade pre 1.1 files.
	return m_ignoreProgramChange || (module() && module()->Container()->getIgnoreProgramChange());
}

void PatchParameter_base::SetProgram()
{
	//		if( !isOutput Parameter() ) // todo work out new way of determining this
	if( !ignoreProgramChange() )
	{
		//		vst_inhibit_send_automation = true;
		//		const int voice = 0;
		//		Update DspValue(voice, true);
		//		vst_inhibit_send_automation = false;
		UpdateGui();	// update in-gui objects
		UpdateGui(FT_NORMALIZED);
	}
}

// provide value in un-typesafe way, for passing via events. small objects in p_int  (large objects return ptr to allocated mem)
void PatchParameter_base::SerialiseValue( my_output_stream& p_stream )
{
	const auto raw = GetValue(FT_VALUE);

	if( typeIsVariableSize() )
	{
		p_stream << (int)raw.size();
	}

	p_stream.Write(raw.data(), (int)raw.size());
}

template<>
void PatchParameter<MpBlob, MetaData_none>::UpdateDspMetaData()
{
}

template<>
void PatchParameter<std::string, MetaData_filename8>::UpdateDspMetaData()
{
}

template<>
void PatchParameter<std::wstring, MetaData_filename>::UpdateDspMetaData()
{
}

template<>
void PatchParameter<std::wstring, MetaData_none>::UpdateDspMetaData()
{
}
template<>
void PatchParameter<float, MetaData_float>::UpdateDspMetaData()
{
	if( m_patch_mgr ) // 0 during serialise
	{
		auto que = MessageQueToDsp();

		if( que )
		{
			my_msg_que_output_stream strm( que, Handle(), "meta" );
			strm << (int) sizeof(float) * 2; // msg length.
			strm << MetaData()->getRangeMinimum();
			strm << MetaData()->getRangeMaximum();
			strm.Send();
		}
	}
}

template<>
void PatchParameter<int, MetaData_int>::UpdateDspMetaData()
{
	if (m_patch_mgr) // 0 during serialise
	{
		if (auto que = MessageQueToDsp(); que)
		{
			my_msg_que_output_stream strm(que, Handle(), "meta");
			strm << (int)sizeof(int32_t) * 2; // msg length.
			strm << MetaData()->getRangeMinimum();
			strm << MetaData()->getRangeMaximum();
			strm.Send();
		}
	}
}
template<>
void PatchParameter<bool, MetaData_none>::UpdateDspMetaData()
{
}
//void PatchParameter<short, MetaData_enum>::UpdateDspMetaData()
template<>
void PatchParameter<int, MetaData_enum>::UpdateDspMetaData()
{
	if( m_patch_mgr ) // 0 during serialise
	{
		auto que = MessageQueToDsp();

		if( que )
		{
			my_msg_que_output_stream strm( que, Handle(), "meta" );
			strm << (int32_t)(sizeof(int) + MetaData()->getEnumList().length() * sizeof(wchar_t)); // msg length.
			strm << MetaData()->getEnumList();
			strm.Send(); // !! put in dstcr?
		}
	}
}

void PatchParameter_base::OnDspMsg(int p_msg_id, my_input_stream& p_stream)
{
	assert( code_to_long('p','p','c',0) == gmpi::hosting::id_to_long("ppc") ); // check same byte order

	switch( p_msg_id )
	{
	case code_to_long('p','p','c',0): // "ppc" Patch parameter change. Either Output parameter or MIDI automation
	{
		p_stream >> vst_inhibit_send_automation;
		dsp_SerialiseValue(p_stream);
		vst_inhibit_send_automation = false;
	}
	break;

	case code_to_long('p','p','c','a'): // Patch parameter change. VST Automation from Host. VST2 only?
	{
		float normalised_value;
		p_stream >> normalised_value;
		setValueNormalised(normalised_value, false);
	}
	break;

	case code_to_long('l','e','r','n'): // "lern" Controller ID change
	{
		p_stream >> m_automation;
		UpdateGui( FT_AUTOMATION );
	}
	break;

	default:
		ui_msg_target::OnDspMsg( p_msg_id, p_stream );
	};

	/*
		if( p_msg_id == id_to_long("pp c") ) // Patch parameter change. Either Output parameter or automation
		{
			p_stream >> vst_inhibit_send_automation;
			dsp_Seriali seValue(p_stream);
			vst_inhibit_send_automation = false;
		}

	*/
}

// VST Get Text Value for display.
template<>
std::wstring PatchParameter<std::string, MetaData_filename8>::vst_getValueAsText()
{
	const auto raw = GetValue(FT_VALUE);
	std::string v = RawToValue<std::string>(raw.data(), raw.size());
	return Utf8ToWstring(v);
}

template<>
std::wstring PatchParameter<std::wstring, MetaData_filename>::vst_getValueAsText()
{
	const auto raw = GetValue(FT_VALUE);
	std::wstring v = RawToValue<std::wstring>(raw.data(), raw.size());
	return v;
}

template<>
std::wstring PatchParameter<std::wstring, MetaData_none>::vst_getValueAsText()
{
	const auto raw = GetValue(FT_VALUE);
	std::wstring v = RawToValue<std::wstring>(raw.data(), raw.size());
	return v;
}

template<>
std::wstring PatchParameter<float, MetaData_float>::vst_getValueAsText()
{
	const auto raw = GetValue(FT_VALUE);
	float v = RawToValue<float>( raw.data(), raw.size() );
	return FloatToString( v );
}

template<>
std::wstring PatchParameter<int, MetaData_enum>::vst_getValueAsText()
{
	const auto raw = GetValue(FT_VALUE);
	int v = RawToValue<int>( raw.data(), raw.size() );
	return enum_get_substring( std::wstring(MetaData()->getEnumList().c_str()), v );
}

template<>
std::wstring PatchParameter<bool, MetaData_none>::vst_getValueAsText()
{
	const auto raw = GetValue(FT_VALUE);
	bool v = RawToValue<bool>( raw.data(), raw.size() );

	if( v )
		return (L"true");
	else
		return (L"false");
}

template<>
std::wstring PatchParameter<int, MetaData_int>::vst_getValueAsText()
{
	const auto raw = GetValue(FT_VALUE);
	int v = RawToValue<int>( raw.data(), raw.size() );
	return IntToString( v );
}

template<>
std::wstring PatchParameter<MpBlob, MetaData_none>::vst_getValueAsText()
{
	return {}; //TODO !!!
}

struct entitystruxt
{
	char* encoded;
	int size;
	char character;
};

template<>
std::string PatchParameter<std::wstring, MetaData_none>::ToXmlStringSafe(int voice, int patch, int /*precision*/)
{
	const auto data = patchMemory[voice]->GetValue(patch);
	const auto size = patchMemory[voice]->GetValueSize(patch);
	return WStringToUtf8(RawToValue<std::wstring>(data, size));
}

template<>
std::string PatchParameter<std::string, MetaData_filename8>::ToXmlStringSafe(int voice, int patch, int /*precision*/)
{
	const auto data = patchMemory[voice]->GetValue(patch);
	const auto size = patchMemory[voice]->GetValueSize(patch);
	return RawToValue<std::string>(data, size);
}

template<>
std::string PatchParameter<std::wstring, MetaData_filename>::ToXmlStringSafe(int voice, int patch, int /*precision*/)
{
	const auto data = patchMemory[voice]->GetValue(patch);
	const auto size = patchMemory[voice]->GetValueSize(patch);
	return WStringToUtf8(RawToValue<std::wstring>(data, size));
}

template<>
std::string PatchParameter<MpBlob, MetaData_none>::ToXmlStringSafe(int voice, int patch, int /*precision*/)
{
	// TODO : Would be nice to avoid copying so much
	const string binaryData((const char*)patchMemory[voice]->GetValue(patch), patchMemory[voice]->GetValueSize(patch));
	return Base64::encode(binaryData);
}

void PatchParameter_base::OnPopupParameterMenu( int choice )
{
	int temp;

	// 0 not used as it gets passed erroneously during init
	if( choice == 1 ) // learn
	{
		temp = ControllerType::Learn;
		SetValueRaw(FT_AUTOMATION, &temp, sizeof(temp) );
	}

	if( choice == 2 ) // un-learn
	{
		temp = ControllerType::None;
		SetValueRaw(FT_AUTOMATION, &temp, sizeof(temp) );
	}

	if( choice == 3 ) // Set via dialog
	{
		getPatchManager()->Container()->Document()->Application()->VO_Notify(OM_SHOW_MIDI_AUTOMATION_DIALOG, (void*) this);
	}
}

// Receive new value from DSP.
void PatchParameter_base::dsp_SerialiseValue(my_input_stream& p_stream)
{
	constexpr int patch = -1; // current patch
	
	int size = typeIsVariableSize() ? 0 : (int)GetValue(FT_VALUE).size();
	std::string temp;

	int voice{};
	p_stream >> voice;
	while (voice != -1) // indicates end-of-list.
	{
		if (typeIsVariableSize())
		{
			p_stream >> size;
		}

		temp.resize(size);
		p_stream.Read(temp.data(), size);
		m_value_being_set_from_dsp = true;
		SetValueRaw(FT_VALUE, temp.data(), size, voice, patch);
		m_value_being_set_from_dsp = false;

		// next voice?
		p_stream >> voice;
	}

	// Emulate Mouse down when automated from MIDI/Processor
	const int timerResetValue = 4;
	if (!m_grabbed)
	{
		m_grabbed = true;
		m_grabbed_by_MIDI_timer = timerResetValue;
		StartTimer(50);
		UpdateGui(FT_GRAB);
	}

	// ALready grabbed and more MIDI arrives, refresh timer.
	if (m_grabbed && m_grabbed_by_MIDI_timer > 0)
	{
		m_grabbed_by_MIDI_timer = timerResetValue;
	}
}

bool PatchParameter_base::OnTimer()
{
	bool done = m_grabbed_by_MIDI_timer-- <= 0;
	if (done)
	{
		if (m_grabbed)
		{
			m_grabbed = false;
			UpdateGui(FT_GRAB);
		}
	}

	return !done;
}

void PatchParameter_base::CopyPatch( int from, int to_patch_lo, int to_patch_hi)
{
	if( ignoreProgramChange() )
		return;

	const int voice = 0;
	const auto raw = GetValue(FT_VALUE);

	for( int x = to_patch_lo ; x <= to_patch_hi ; x++ )
	{
		SetValue(raw, FT_VALUE, voice, x );
	}
}

void PatchParameter_base::setIgnoreProgramChange(bool value)
{
	m_ignoreProgramChange = value;
}

void PatchParameter_base::onSetIgnoreProgramChange()
{
	if( patchMemory.size() == 0 ) // if patch mem not initialized yet. quietly exit.
	{
		return;
	}

	int patchCount = calcPatchMemorySize();
	int voiceCount = (int) patchMemory.size();

	for(int i = 0 ; i < voiceCount ; i++ )
	{
		patchMemory[i]->setPatchCount(patchCount);
	}

	// GUI should revert to patch 1
	UpdateGui();	// update in-gui objects
	UpdateGui(FT_NORMALIZED);
}

int PatchParameter_base::calcPatchMemorySize()
{
	int patchCount = 128;

	if( ignoreProgramChange() )
	{
		patchCount = 1;
	}

	return patchCount;
}

int PatchParameter_base::ModuleHandle()
{
	// Can't just store module handle, as it changes during a paste operation.
	// resulting in new param having module handle of 'copied' param.
	if(module_)
		return module()->Handle();

	return -1;
}

int32_t PatchParameter_base::RegisterWatcher(IPluginGui* observer)
{
	watchers.push_back(observer);
	return 0;
}

int32_t PatchParameter_base::UnRegisterWatcher(IPluginGui* observer)
{
	auto it = find( watchers.begin(), watchers.end(), observer );

	if ( it != watchers.end() )
	{
		watchers.erase(it);
	}

	return 0;
}



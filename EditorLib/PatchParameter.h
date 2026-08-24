#pragma once

/*
#include "PatchParameter.h"
*/

#include <map>
#include <string>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include "conversion.h"
#include "datatype_to_id.h"
#include "./dsp_patch_parameter.h"

#include "./PatchManager.h"
#include "IPluginGui.h"
#include "modules/se_sdk3/mp_sdk_common.h"
#include "./MyTypeTraits.h"
#include "./RawConversions.h"
#include "PatchStorage.h"
#include "TypeToEDatatype.h"
#include "UgDatabase.h"
#include "modules/se_sdk3/TimerManager.h"


class PatchParameter_base : public ui_msg_target, public gmpi::hosting::QueClient, public se_sdk::TimerClient
{
	friend class CPatchManager;

	void setDocModified();
//	void setPresetModified();
	void initStates();

public:
	PatchParameter_base();
	PatchParameter_base( CUG* p_module, parameter_description& descriptor );
	virtual ~PatchParameter_base();
	virtual bool instansiateDsp()
	{
		return true;
	}
	void InitializePatchMemory(bool setDefaults = true, int patchCount = -1 ); // -1 = determin size.

	// IGuiHostParameter support.
	virtual int32_t Release()
	{
		return 0;
	} // todo, full ref counting etc.
	virtual int32_t CanAutomate( bool* returnValue)
	{
		*returnValue = can_automate() && !is_private();
		return 0;
	}
	virtual int32_t IsPolyphonic( bool* returnValue)
	{
		*returnValue=isPolyphonic();
		return gmpi::MP_OK;
	}
	virtual int32_t IsReadOnly( bool* returnValue)
	{
		*returnValue = is_read_only();
		return 0;
	}
	virtual int32_t IsPrivate( bool* returnValue)
	{
		*returnValue = is_private();
		return 0;
	}
	virtual int32_t GetHandle( int* returnValue)
	{
		*returnValue = Handle();
		return 0;
	}

	virtual int32_t GetDatatype(enum ParameterFieldType field, int* returnValue);

	// Preferred byte-level accessors. 'field', 'voice' and 'patch' have sensible defaults.
	// These are the public face of the low-level GetValueImpl/SetValueRaw override-points
	// below (which are protected implementation detail).
	void SetValue(RawView raw, ParameterFieldType field = FT_VALUE, int voice = 0, int patch = -1)
	{
		SetValueRaw(field, raw.data(), static_cast<int>(raw.size()), voice, patch);
	}
	RawView GetValue(ParameterFieldType field = FT_VALUE, int voice = 0, int patch = -1)
	{
		return GetValueImpl(field, voice, patch);
	}

	virtual int32_t RegisterWatcher(IPluginGui*);
	virtual int32_t UnRegisterWatcher( IPluginGui* );

	template< class Serializer >
	void SerialiseB(Archive2& ar)
	{
		UniqueSnowflake::SerialiseB<Serializer>(ar); // call base-class.

		Serializer s(ar.xmlElement);

		s("shortName", m_short_name);
		s("ignorePC", m_ignoreProgramChange);
		s("automation", m_automation, -1);
		s("sysex", m_automation_sysex);
		s("private", isPrivate);
		s("vstParameterNumber", vstParameterNumber_, -1);
		s("stateful", isStateful, true);
		s("vstDisplayType", vstDisplayType_);
		s("vstDisplayMin", vstDisplayMin_);
		s("vstDisplayMax", vstDisplayMax_);
//		s("ppname", m_plug_param_name);
//		s("moduleParameterId", moduleParameterId_);
		s("isPolyphonic", isPolyphonic_);
		s("isPolyphonicGate", isPolyphonicGate_);
		s("isFilenameWritable", isFilenameWritable_);
		s("hint", hint);
	}

	void Serialize2(Archive2& ar)
	{
		if (ar.isSaving)
			SerialiseB<XmlSaveHelper>(ar);
		else
			SerialiseB<XmlLoadHelper>(ar);
	}

	double GetValueReal(enum ParameterFieldType FieldId = FT_VALUE, int voice = 0, int patch = -1);
	int GetValueInt(enum ParameterFieldType FieldId = FT_VALUE, int voice = 0, int patch = -1);
	std::wstring GetValueString(enum ParameterFieldType FieldId = FT_VALUE, int voice = 0, int patch = -1);

	void ExportXmlPreset(TiXmlElement* XmlParent, bool isSinglePreset, int presetIndex);
	virtual void ExportXml(class TiXmlElement* XmlParent, ExportFormatType targetType);
	virtual tinyxml2::XMLElement* Export(tinyxml2::XMLElement* parameters_xml, ExportFormatType targetType);
	virtual void Import(tinyxml2::XMLElement* parameter_xml, ExportFormatType targetType);
	bool doesExportToPlugin(ExportFormatType targetType);
	bool WavesIsParamNormalized();
	int WavesGetPrecision(float WvMin, float WvMax, float* returnResolution = 0 );
	float getVstDisplayMin()
	{
		if( WavesIsParamNormalized() )
		{
			return 0;
		}
		return vstDisplayMin_;
	}
	float getVstDisplayMax()
	{
		if( WavesIsParamNormalized() )
		{
			return 1;
		}
		return vstDisplayMax_;
	}

	virtual void SerialiseValue(gmpi::hosting::my_output_stream& p_stream );  // provide value in un-typesafe way, for passing via events. small objects in p_int  (large objects return ptr to allocated mem)
	virtual void SerialiseMetaData(gmpi::hosting::my_output_stream& p_stream )=0;  // provide value in un-typesafe way, for passing via events. small objects in p_int  (large objects return ptr to allocated mem)
	virtual void dsp_SerialiseValue(gmpi::hosting::my_input_stream& p_stream );
	virtual void SetProgram();
	virtual int ClassTypeId()=0;
	virtual int DspDataTypeID()=0;
	virtual bool typeIsVariableSize() = 0;
	virtual void UpdateDspMetaData() {}
	bool isPolyphonic()
	{
		return isPolyphonic_;
	}
	void setPolyphonic(bool value)
	{
		isPolyphonic_ = value;
	}
	bool isPolyphonicGate()
	{
		return isPolyphonicGate_;
	}
	void setPolyphonicGate(bool value)
	{
		isPolyphonicGate_ = value;
	}
	// Only meaningful on a filename parameter (one with a file-extension): true if the
	// module writes the file, false if it only reads it. See IO_FILENAME_WRITABLE.
	bool isFilenameWritable() const
	{
		return isFilenameWritable_;
	}
	void setIgnoreProgramChange(bool value);
	void onSetIgnoreProgramChange();
	bool is_stateful() const
	{
		return (bool) isStateful;
	}

	bool is_saved_in_project()
	{
		// Host controls don't get saved in project file, unless stateful (Oversampling & Polyphony).
		// Orphaned module-owned host-controls are physically removed by
		// CContainer::RemoveOrphanedHostControls() (on delete and at save), so a surviving
		// stateful host-control is by definition still in use.
		return hostControlId_ == HC_NONE || is_stateful();
	}
	virtual bool can_automate()
	{
		return true;
	}
	virtual bool is_read_only() const
	{
		return false;
	}
	bool is_private() const
	{
		return (bool) isPrivate;
	}
	void setPrivate(bool value)
	{
		isPrivate = value;
	}
	virtual bool can_rename()
	{
		return true;
	}
	virtual void setValueNormalised(float p_normalised, bool applyDawAjustment) = 0;
	virtual float getValueNormalised( int /*patch*/ = -1, int /*voice*/ = 0 )
	{
		return 0.f;
	}
	virtual std::wstring vst_getValueAsText() = 0;

	virtual std::string ToXmlStringSafe(int voice, int patch, int precision = -1) = 0;
	
	virtual void FromXmlString(const std::string& xmlstring, int voice, int patch)
	{
		int datatype;
		GetDatatype(FT_VALUE, &datatype);
		auto raw = ParseToRaw(datatype, xmlstring);
		SetValueRaw(FT_VALUE, raw.data(), (int)raw.size(), voice, patch);
	}

	virtual void CopyPatch( int from, int to_patch_lo, int to_patch_hi);

	bool IsCopyTagged();
	std::string wavesParameterlabel_;

	inline CUG* module()
	{
		return module_;
	}
	void setModule(CUG* module)
	{
		module_ = module;
	}
	void setParameterId(int pinId);

	virtual void setDefault( const std::wstring& /*defaultValueString*/ ) {}
	void UpdateDspValue( int patch, int voice );
	void setPatchManager(CPatchManager* p_patch_mgr)
	{
		m_patch_mgr=p_patch_mgr;
	}
	CPatchManager* getPatchManager()
	{
		return m_patch_mgr;
	}
	virtual void UpdateGui(ParameterFieldType paramType = FT_VALUE, int voice = 0);
	void OnDspMsg(int p_msg_id, gmpi::hosting::my_input_stream& p_stream) override;
	virtual int getPatchCount();

	void SetVstParameterNumber( int index )
	{
		vstParameterNumber_ = index;
	}
	int GetVstParameterNumber() const
	{
		return vstParameterNumber_;
	}

	int ModuleHandle();
	int ModuleParameterId()
	{
		return moduleParameterId_;
	}
	void setSdk2BackwardCompatibility(bool v)
	{
		Sdk2BackwardCompatibilityFlag_ = v;
	}
	bool ignoreProgramChange();
	void setStateful(bool pisStateful)
	{
		// Defaults to true. Only SDK3 modules can set this false via "persistant" element.
		isStateful = pisStateful;
	}
	std::wstring GetName() const
	{
		return m_short_name;
	}
	void SetName(std::wstring name)
	{
		m_short_name = name;
	}
	virtual bool OnTimer() override;
	virtual int getVoiceCount() = 0;

	// QueClient
	int queryQueMessageLength(int availableBytes) override;
	void getQueMessage(gmpi::hosting::my_output_stream& outStream, int messageLength) override;

public:
	gmpi_forms::State<bool> isStateful{ true }; // except for host-generated parameters
	gmpi_forms::State<bool> isPrivate; // hidden from VST hosts, don't send automation
	gmpi_forms::State<bool> m_ignoreProgramChange{ true };

	HostControls hostControlId_ = HC_NONE;
	int m_automation;
	std::wstring m_short_name;
	std::string hint;

	bool vst_inhibit_send_automation = false; // value set from VST host (so don't send it back via setparameterautomated. kicks Cubase out of automation record mode)

	// WPF notification.
private:
	std::vector<IPluginGui*> watchers;

protected:
	// Low-level byte accessors. Implementation detail / override-points behind the
	// public RawView GetValue()/SetValue() API. Callers should use those instead.
	virtual RawView GetValueImpl(ParameterFieldType field, int voice = 0, int patch = -1 );
	virtual void SetValueRaw(ParameterFieldType field, const void* data, int size, int voice = 0, int patch = -1);

	void clearPatchMemory();
	int EffectivePatch()
	{
		if( ignoreProgramChange() )
			return 0;
		else
			return m_patch_mgr->GetProgram();
	}
	void OnPopupParameterMenu( int choice );
	gmpi::hosting::IWriteableQue* MessageQueToDsp();
	void UpdateDspControllerId();
	void UpdateDspSysex();

	template<typename T>
	void UpdateDsp(T value, const char* p_msg_id)
	{
		auto que = MessageQueToDsp();

		if (que)
		{
			auto messageSize = static_cast<int>(RawSize(value) + (IsVariableSize(value) ? sizeof(int) : 0));
			gmpi::hosting::my_msg_que_output_stream strm(que, Handle(), p_msg_id);
			strm << messageSize;
			strm << value;
			strm.Send();
		}
	}

	std::wstring fullPath();
	int calcPatchMemorySize();

	CPatchManager* m_patch_mgr = nullptr;
	std::wstring m_automation_sysex;
	bool m_grabbed = false;
	int m_grabbed_by_MIDI_timer = 0;
	bool m_value_being_set_from_dsp = false; // so don't send it back to DSP
	std::vector<class PatchStorageBase*> patchMemory;

protected:
	std::string GetRaw2TemporaryStorage_;

private:
	CUG* module_ = {};
	int moduleParameterId_ = -1;
	bool isPolyphonic_ = false;
	bool isPolyphonicGate_ = false;
	bool isFilenameWritable_ = false;
	int vstParameterNumber_ = -1;
	int vstDisplayType_ = 0;
	float vstDisplayMin_ = 0;
	float vstDisplayMax_ = 0;
	bool Sdk2BackwardCompatibilityFlag_ = false; // indicates all DSP updates must be sent (no que thinning).
};

// WARNING: don't provide function bodies for members you want to specialise,
// MSVC 7 will ignore the specialised version (due to poor conformance)

template <typename T, class MetaDataPolicy = MetaData_none>
class PatchParameter : public PatchParameter_base
{
public:
	PatchParameter() // for serialisation and host-generated.
	{
	}

	PatchParameter( CUG* p_module, parameter_description& descriptor ) : PatchParameter_base( p_module, descriptor )
	{
		MyTypeTraits<T>::parse( descriptor.defaultValue.c_str(), m_default_value );
		metadata_.parse(descriptor.metaData);
	}

	// IGuiHostParameter support.
	int32_t Release() override
	{
		return 0;
	} // todo, full ref counting etc.
	int32_t GetDatatype(enum ParameterFieldType field, int* returnValue) override
	{
		switch(field)
		{
		case FT_VALUE:
		case FT_DEFAULT:
			*returnValue = (int) TypeToEDatatype<T>::enum_value;
			break;

		case FT_RANGE_LO:
		case FT_RANGE_HI:
		case FT_ENUM_LIST:
		case FT_FILE_EXTENSION:
			return MetaData()->GetDatatype( field, returnValue );
			break;

		default:
			return PatchParameter_base::GetDatatype(field, returnValue);
		};

		return gmpi::MP_OK;
	}

	void setValueNormalised(float p_normalised, bool applyDawAjustment) override
	{
		T value;

		if( MetaData()->ValueFromNormalised( p_normalised, value, applyDawAjustment ) )
		{
			SetPatchValue( EffectivePatch(), value );
		}
	}
	float getValueNormalised( int patch = -1, int voice = 0 ) override
	{
		const auto raw = GetValue(FT_VALUE, voice, patch);
		T v = RawToValue<T>( raw.data(), raw.size() );
		return MetaData()->NormalisedFromValue( v );
	}

	std::wstring vst_getValueAsText() override;

	// most datatypes can be converted to string via ostringstream
	std::string ToXmlStringSafe(int voice, int patch, int precision = -1) override
	{
		auto data = patchMemory[voice]->GetValue(patch);
		std::ostringstream oss;
		T value = RawToValue<T>(data, patchMemory[voice]->GetValueSize(patch));

		// Use default format (removes trailing zeros)
		if (precision > -1)
		{
			oss << std::setprecision(precision) << value;
		}
		else
		{
			oss << value;
		}

		// Unless it outputs scientific notation (on small numbers)
		// Then switch to fixed style.
		if (oss.str().find_first_of('e') != std::string::npos)
		{
			oss.str("");
			oss << setiosflags(std::ios_base::fixed) << std::setprecision(precision) << value;
		}

		return oss.str();
	}
		
	tinyxml2::XMLElement* Export(tinyxml2::XMLElement* parameters_xml, ExportFormatType targetType) override
	{
		auto parameter_xml = PatchParameter_base::Export(parameters_xml, targetType);
		if (parameter_xml)
		{
			metadata_.Export(parameter_xml, targetType);
		}

		return parameter_xml;
	}

	void Import(tinyxml2::XMLElement* parameter_xml, ExportFormatType targetType) override
	{
		PatchParameter_base::Import(parameter_xml, targetType);
		metadata_.Import(parameter_xml, targetType);
	}

	void SerialiseMetaData(gmpi::hosting::my_output_stream& p_stream ) override
	{
		metadata_.SerialiseMetaData(p_stream);
	}
	MetaDataPolicy* MetaData()
	{
		return &metadata_;
	}
	void UpdateDspMetaData() override;

protected:
	// Low-level byte accessors (override-points behind the public GetValue/SetValue).
	// assume set from GUI at present
	void SetValueRaw(ParameterFieldType field, const void* data, int size, int voice = 0, int patch = -1) override
	{
		switch(field)
		{
		case FT_DEFAULT:
			m_default_value = RawToValue<T>(data, size);
			break;

		case FT_RANGE_LO:
		case FT_RANGE_HI:
		case FT_ENUM_LIST:
		case FT_FILE_EXTENSION:
			MetaData()->SetValueRaw( field, data, size );
			UpdateDspMetaData();
			UpdateGui(field);
			break;

		default:
			PatchParameter_base::SetValueRaw(field, data, size, voice, patch);
			return; // avoid updating views twice.
		};

		if( m_patch_mgr && (patch == -1 || patch == EffectivePatch()) )
		{
			//UpdateGui( field, voice );	// update views
			m_patch_mgr->OnParameterUpdate( this, field, voice, data, size );
		}
	}
	RawView GetValueImpl(ParameterFieldType field, int voice = 0, int patch = -1) override
	{
		switch (field)
		{
		case FT_DEFAULT:
			return RawView( RawData3(m_default_value), RawSize(m_default_value) );

		case FT_RANGE_LO:
		case FT_RANGE_HI:
		case FT_ENUM_LIST:
		case FT_FILE_EXTENSION:
		{
			const void* t;
			int s;
			MetaData()->GetValueRaw2(field, &t, &s);
			return RawView( t, s );
		}

		default:
			return PatchParameter_base::GetValueImpl(field, voice, patch);
		};
	}

public:
	void SetPatchValue( int patch, const T& newValue )
	{
		int voice = 0;
		PatchParameter_base::SetValueRaw( FT_VALUE, RawData3(newValue), RawSize(newValue), voice, patch );

		// notify if current patch changed
		if( patch == EffectivePatch() ) //m_patch_mgr->GetProgram() )
		{
			UpdateGui();	// update in-gui objects
		}
	}
	void setDefault( const std::wstring& defaultValueString ) override
	{
		MyTypeTraits<T>::parse( defaultValueString.c_str(), m_default_value );
		UpdateGui(FT_DEFAULT);
	}
	
	int getVoiceCount() override
	{
		return static_cast<int>(patchMemory.size());
	}

protected:
	T m_default_value = {};

private:
	bool typeIsVariableSize() override
	{
		return MyTypeTraits<T>::IsVariableLengthType;
	}

	MetaDataPolicy metadata_;

	// persistance (serialisation)
	int ClassTypeId() override
	{
		return DatatypeToId< PatchParameter< T, MetaDataPolicy> >::DataTypeId();
	}
	int DspDataTypeID() override
	{
		return DatatypeToId< dsp_patch_parameter< T, MetaDataPolicy> >::DataTypeId();
	}
};

typedef PatchParameter<MpBlob, MetaData_none> PatchParameterBlob;

// BLOB Specialisations
template<>
std::string PatchParameter<MpBlob, MetaData_none>::ToXmlStringSafe(int voice, int patch, int precision);

template<>
std::string PatchParameter<std::string, MetaData_filename8>::ToXmlStringSafe(int voice, int patch, int /*precision*/);
template<>
std::string PatchParameter<std::wstring, MetaData_filename>::ToXmlStringSafe(int voice, int patch, int /*precision*/);

template<>
std::string PatchParameter<std::wstring, MetaData_none>::ToXmlStringSafe(int voice, int patch, int /*precision*/);

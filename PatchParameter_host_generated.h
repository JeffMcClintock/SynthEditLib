#pragma once
#include <sstream>
#include "PatchParameter.h"
#include "midi_defs.h"
#include "SynthEditDocBase.h"
#include "CContainer.h"
// HOST_GENERATED_PARAMETERS. might be better to use technique of Ctl_vstPlugin !!!

template <typename T, class MetaDataPolicy = MetaData_none>
class PatchParameter_host :
	public PatchParameter_base
{
public:
	PatchParameter_host()
	{
		//	setStateful(false); // prevent serialisation
	}
	virtual bool instansiateDsp() override
	{
		return false;
	} // because initializeDsp() crashes. may need fixing.

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
			metadata_.GetDatatype( field, returnValue );
			break;

		default:
			return PatchParameter_base::GetDatatype(field, returnValue);
		};

		return gmpi::MP_OK;
	}
	// persistance (serialisation)
	int ClassTypeId() override
	{
		assert(false);
		return -1;
	}
	//bool IsHostGenerated(){return true;};
	bool typeIsVariableSize() override
	{
		return MyTypeTraits<T>::IsVariableLengthType;
	}
	int DspDataTypeID() override
	{
		return DatatypeToId< dsp_patch_parameter< T, MetaDataPolicy> >::DataTypeId();
	}

	void setValueNormalised(float p_normalised, bool applyDawAjustment) override
	{
		assert(false); // not implemented
	}

	// override these
	virtual T GetValue( int voice = 0 ) = 0;
	virtual void SetValue( T, int voice = 0 ) = 0;

	virtual void SerialiseValue(gmpi::hosting::my_output_stream& p_stream, int patch )
	{
		p_stream << GetValue();
	}
	void SerialiseMetaData(gmpi::hosting::my_output_stream& p_stream ) override
	{
		//m_control.Feature()->SerialiseMetaData(p_stream);
	};
	void dsp_SerialiseValue(gmpi::hosting::my_input_stream& p_stream ) override
	{
		T temp;
		p_stream >> temp;
		m_value_being_set_from_dsp = true;
		SetValue(temp);
		m_value_being_set_from_dsp = false;
	}
	std::wstring vst_getValueAsText() override
	{
		//TODO, also 4 PatchParameter
		// return ToString( GetValue() ); // use same technique as getvalueraw
		return L"";
	}
	std::string ToXmlStringSafe(int voice, int patch, int /*precision*/) override
	{
		assert(false);
		return {};
	}

	void FromXmlString(const std::string& xmlstring, int voice, int patch) override {}

	void SetProgram() override {}
	void CopyPatch( int from, int to_patch_lo, int to_patch_hi) override {}

protected:
	// Low-level byte accessors (override-points behind the public GetValue/SetValue).
	void SetValueRaw(ParameterFieldType field, const void* data, int size, int voice = 0, int patch = -1) override
	{
		bool needSave = true;

		switch(field)
		{
		case FT_VALUE:
			SetValue( RawToValue<T>(data, size) );
			break;

		case FT_SHORT_NAME: // read only
			break;

		case FT_DEFAULT:
			assert(false && "can't set host generated param default");
			//			m_default_value = RawToValue<T>(data, size);
			break;

		case FT_RANGE_LO:
		case FT_RANGE_HI:
		case FT_ENUM_LIST:
		case FT_FILE_EXTENSION:
			MetaData()->SetValueRaw( field, data, size );
			needSave = false;
			break;

			/*
					case FT_MENU_ITEMS: // read only
						break;
					case FT_MENU_SELECTION:
						OnPopupParameterMenu( RawToValue<int>(data, size) );
						break;
			*/
		default:
			PatchParameter_base::SetValueRaw( field, data, size, voice, patch);
			return;
		};

		// update other views.
		if( m_patch_mgr )
			m_patch_mgr->OnParameterUpdate( this, field, voice, data, size );

		if( needSave )
		{
			getPatchManager()->Container()->Document()->SetModified();
		}
	}

	RawView GetValueImpl(ParameterFieldType field, int voice = 0, int patch = -1) override
	{
		switch (field)
		{
		case FT_VALUE:
			GetRaw2TemporaryStorage_ = ToRaw4(GetValue());
			return RawView( GetRaw2TemporaryStorage_.data(), GetRaw2TemporaryStorage_.size() );

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

		case FT_HOST_PARAMETER:
			GetRaw2TemporaryStorage_ = ToRaw4(true);
			return RawView( GetRaw2TemporaryStorage_.data(), GetRaw2TemporaryStorage_.size() );

		default:
			return PatchParameter_base::GetValueImpl(field, voice, patch);
		};
	}

public:
	MetaDataPolicy* MetaData()
	{
		return &metadata_;
	}
	int getVoiceCount() override
	{
		return 1;
	}
	int getPatchCount() override
	{
		return 1;
	}

private:
	MetaDataPolicy metadata_;
};

// HOST_GENERATED_PARAMETERS

class PatchParameter_host_Program :
	public PatchParameter_host<int,MetaData_int>
{
public:
	PatchParameter_host_Program()
	{
		m_short_name = L"Program";
		MetaData()->setRangeHi(127);
	}
	int GetValue(int voice = 0) override
	{
		return getPatchManager()->GetProgram();
	}
	void SetValue(int p_new_value, int voice = 0) override
	{
		getPatchManager()->SetProgram(p_new_value);
	}
	virtual void setValueNormalised(float p_normalised)
	{
		int patch = (int) (p_normalised * 127.0f);

		if( patch < 0 )
			patch = 0;

		if( patch > 127 )
			patch = 127;

		SetValue( patch );
	}

	void SetProgram() override
	{
		// update in-gui objects
		UpdateGui();
	}
};

class PatchParameter_host_ProgramName :
	public PatchParameter_host<std::wstring,MetaData_filename> // todo don't need MetaData_filename. requires writting support for that type of parameter
{
public:
	PatchParameter_host_ProgramName()
	{
		m_short_name = L"Program Name";
	}
	std::wstring GetValue(int voice = 0) override
	{
		return getPatchManager()->getProgramNameIndexed();
	}
	void SetValue(std::wstring p_new_value, int voice = 0) override
	{
		getPatchManager()->setProgramNameIndexed(p_new_value);
	}
	void SetProgram() override
	{
		// update in-gui objects
		UpdateGui();
	}
	// Need program name available in VST3 plugins processor, so can save it in preset XML.
	// this parameter should just be a regualar parameter like "Program Category",
	// but to minimise disruption, just hack this for now.
	bool instansiateDsp() override
	{
		return true;
	}

	std::string ToXmlStringSafe(int voice, int patch, int precision = -1) override
	{
		return WStringToUtf8(getPatchManager()->getProgramNameIndexed(patch));
	}
};

class PatchParameter_host_ProgramNamesList :
	public PatchParameter_host<std::wstring,MetaData_filename> // todo don't need MetaData_filename. requires writting support for that type of parameter
{
public:
	PatchParameter_host_ProgramNamesList()
	{
		m_short_name = L"Program Names List";
	}
	std::wstring GetValue(int voice = 0) override
	{
		int program_count = getPatchManager()->getNumPrograms();
		std::wostringstream oss;
		bool first = true;

		for( int i = 0 ; i < program_count ; i++ )
		{
			if (first)
			{
				first = false;
			}
			else
			{
				oss << L',';
			}

			auto name = getPatchManager()->getProgramNameIndexed(i);
			if (name.empty())
			{
				std::wostringstream oss2;
				oss2 << L"Patch " << setfill(L'0') << std::setw(3) << (i + 1);
				name = oss2.str();
			}

			oss << name;
		}

		return oss.str();
	}
	void SetValue(std::wstring p_new_value, int voice = 0) override {}
};

class PatchParameter_host_ProgramCategoriesList :
	public PatchParameter_host<std::wstring, MetaData_none>
{
public:
	PatchParameter_host_ProgramCategoriesList()
	{
		m_short_name = L"Program Categories List";
	}
	std::wstring GetValue(int voice = 0) override
	{
		int program_count = getPatchManager()->getNumPrograms();
		std::wostringstream oss;
		bool first = true;

		for (int i = 0; i < program_count; i++)
		{
			if (first)
			{
				first = false;
			}
			else
			{
				oss << L',';
			}

			auto name = getPatchManager()->getProgramCategoryIndexed(i);

			oss << name;
		}

		return oss.str();
	}
	void SetValue(std::wstring p_new_value, int voice = 0) override {}
};

class PatchParameter_host_MidiChannelIn :
	public PatchParameter_host<int,MetaData_int>
{
public:
	PatchParameter_host_MidiChannelIn()
	{
		m_short_name = L"Midi Channel In";
		MetaData()->setRangeHi(16);
	}
	int GetValue(int voice = 0) override
	{
		return getPatchManager()->getMidiChannel();
	}
	void SetValue(int p_new_value, int voice = 0) override
	{
		getPatchManager()->setMidiChannel(p_new_value);
	}
};

class PatchParameter_host_PatchCommands : public PatchParameter_host<int,MetaData_enum>
{
public:
	int GetValue(int voice = 0) override
	{
		return 0;
	}
	void SetValue( int p_new_value, int voice = 0 ) override
	{
		getPatchManager()->DoHostCommand(p_new_value);
	}
};


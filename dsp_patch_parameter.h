#pragma once
#include "dsp_patch_parameter_base.h"
#include "./variable_policies.h"
#include "./MyTypeTraits.h"
#include "./RawConversions.h"
#include "./PatchStorage.h"
#include "IDspPatchManager.h"

using namespace std;

template <typename T, class MetaDataPolicy = MetaData_none>
class dsp_patch_parameter :
	public dsp_patch_parameter_base , public MetaDataPolicy
{
public:
	void setMetadataRangeMinimum( double /*rangeMinimum*/ ) override
	{
		// Overriden for specific types below...
	}
	void setMetadataRangeMaximum( double /*rangeMaximum*/ ) override
	{
		// Overriden for specific types below...
	}
	void setMetadataTextMetadata( std::wstring metadata ) override
	{
		MetaDataPolicy::setTextMetadata( metadata );
	}
	int typeSize() override
	{
		return MyTypeTraits<T>::FixedTypeSize;
	}
	int dataType() override
	{
		return MyTypeTraits<T>::DataType;
	}
	bool typeIsVariableSize() override
	{
		return MyTypeTraits<T>::IsVariableLengthType;
	}
	void SerialiseMetaData(my_input_stream& p_stream) override
	{
		MetaDataPolicy::SerialiseMetaData( p_stream );
	}
	void CopyPlugValue( int voice, UPlug* p_plug) override;
	void vst_automate(timestamp_t p_timestamp, int voice, float p_normalised_val, int32_t flags) override
	{
		// If user holding knob, don't MIDI automate.
		// Not sure how VST3 handles this, maybe host handles it.
		if (m_grabbed && 0 != (flags & kIsMidiMappedAutomation))
			return;
        const bool applyDawAjustment = 0 == (flags & kIsMidiMappedAutomation);

		T newValue;

		if( this->ValueFromNormalised(p_normalised_val, newValue, applyDawAjustment))
		{
			vst_automate2(p_timestamp, voice, RawData3(newValue), RawSize(newValue), flags);
		}
	}
	float GetValueNormalised( int voiceId = 0 ) override
	{
		// assert( !typeIsVariableSize() && "Specialize this for variable-size types like strings");

		if( typeIsVariableSize() )
		{
			return 0.0f;
		}

		auto data = patchMemory[voiceId]->GetValue( EffectivePatch() );
		T value = RawToValue<T>( data );
		return this->NormalisedFromValue( value );
	}
	void setValueNormalised(float p_normalised_val) override
	{
		T newValue;

		if( this->ValueFromNormalised(p_normalised_val, newValue, false) )
		{
			int voice = 0;

			if( patchMemory[voice]->SetValue(RawData3(newValue), RawSize(newValue), EffectivePatch() ) )
			{
				OnValueChangedFromGUI(false, voice);
				UpdateUI(true); // true indicates not to call setparamterautomated (sending change back to host kicks Cubase out of automation mode)
			}
		}
	}

	void InitializePatchMemory(const wchar_t* defaultString) override
	{
		T defaultValue;
		MyTypeTraits<T>::parse( defaultString, defaultValue );

		for( int voice = 0 ; voice < (int) patchMemory.size() ; ++voice )
		{
			for( int patch = 0 ; patch < patchMemory[voice]->getPatchCount() ; ++patch )
			{
				patchMemory[voice]->SetValue( RawData3(defaultValue), RawSize(defaultValue), patch );
			}
		}
	}

	bool SetValueFromXml(const std::string& valueString, int voice, int patch) override
	{
		T value;
		MyTypeTraits<T>::parse( valueString.c_str(), value );
		// assert( !typeIsVariableSize() && " specialize this for variable-size types like strings");
		return patchMemory[voice]->SetValue( RawData3(value), RawSize(value), patch );
	}
	std::string GetValueAsXml(int voice) override
	{
		constexpr int patch = 0;
		auto data = patchMemory[voice]->GetValue( patch );
		int size = patchMemory[voice]->GetValueSize( patch );
		T value = RawToValue<T>( data, size );

		return MyTypeTraits<T>::toXML( value );
	}

#if defined( _DEBUG )
    std::wstring GetValueString(int patch) override
	{
		const int voiceId = 0;
		auto data = patchMemory[voiceId]->GetValue( patch );
		int size = patchMemory[voiceId]->GetValueSize( patch );
		return RawToString<T>(data, size);
	}
#endif
};

/*
// APPLE requires this in cpp, MS in header.
#if defined( __GNU_CPP__ )
template <>
void dsp_patch_parameter<wstring, MetaData_filename>::SetValue FromXml( const char* valueString, const char* end, int voice, int patch );
#else
template <>
void dsp_patch_parameter<wstring, MetaData_filename>::SetValue FromXml( const char* valueString, const char* end, int voice, int patch )
{
	std::string v(valueString, end - valueString);
	std::wstring value;
	MyTypeTraits<std::wstring>::par se( v.c_str(), value );
	const void* data = value.data();
	size_t size = sizeof(wchar_t) * value.length();
	patchMemory[voice]->SetValue( data, size, patch );
}
#endif
*/
// Text and Blob can't be normalized.
//template <>
//float dsp_patch_parameter<wstring, MetaData_filename>::GetValueNormalised( int voiceId );
//
//template <>
//float dsp_patch_parameter<MpBlob, MetaData_none>::GetValueNormalised( int voiceId );

template <>
void dsp_patch_parameter<float, MetaData_float>::setMetadataRangeMinimum( double rangeMinimum );

template <>
void dsp_patch_parameter<float, MetaData_float>::setMetadataRangeMaximum( double rangeMaximum );

template <>
void dsp_patch_parameter<int, MetaData_int>::setMetadataRangeMinimum( double rangeMinimum );

template <>
void dsp_patch_parameter<int, MetaData_int>::setMetadataRangeMaximum( double rangeMaximum );

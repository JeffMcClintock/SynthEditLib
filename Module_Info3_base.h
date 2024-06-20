#pragma once

#include "module_info.h"
#include <string>

struct MI3Flags
{
	const char* name;
	int32_t readFlag;
	int32_t writeFlags;
};

class Module_Info3_base : public Module_Info
{
public:
	Module_Info3_base( const wchar_t* moduleId );
	void ScanXml( class TiXmlElement* xmlData ) override;
	virtual ug_base* BuildSynthOb( void ) override;

	void SetupPlugs();
	std::wstring GetDescription() override
	{
		return (m_description);
	}
	int ModuleTechnology() override;
	int getWindowType() override
	{
		return m_window_type;
	}

	int ug_flags;
	std::wstring m_description;
	int m_window_type = MP_WINDOW_TYPE_NONE;

	inline static const MI3Flags flagNames[6] = {
	{ "polyphonicSource"	, UGF_POLYPHONIC_GENERATOR,			UGF_POLYPHONIC_GENERATOR | UGF_POLYPHONIC },
	{ "polyphonicAggregator", UGF_POLYPHONIC_AGREGATOR,			UGF_POLYPHONIC_AGREGATOR },
	{ "polyphonic"			, UGF_POLYPHONIC,					UGF_POLYPHONIC },
	{ "cloned"				, UGF_POLYPHONIC_GENERATOR_CLONED,	UGF_POLYPHONIC_GENERATOR_CLONED | UGF_HAS_HELPER_MODULE },
	{ "voiceMonitorIgnore"	, UGF_VOICE_MON_IGNORE,				UGF_VOICE_MON_IGNORE },
	{ "simdOverwritesBufferEnd", UGF_SSE_OVERWRITES_BUFFER_END, UGF_SSE_OVERWRITES_BUFFER_END },
	};

protected:
	Module_Info3_base(); //serialisation.
};

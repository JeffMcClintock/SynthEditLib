
#include "ug_soundcard_out.h"
#include "resource.h"
#include "module_register.h"
#include "ug_oversampler.h"

SE_DECLARE_INIT_STATIC_FILE(ug_soundcard_out);

namespace
{
REGISTER_MODULE_1(L"Sound Out", IDS_MN_SOUND_OUT,IDS_MG_INPUT_OUTPUT,ug_soundcard_out,CF_STRUCTURE_VIEW,L"Sends audio to your speakers.  You are limited to one soundcard out module.  Non-registered SynthEdit limited to 2 output channels.");
}

#define SOUNDCARD_FADE_TIME 0.005f

// Fill an array of InterfaceObjects with plugs and parameters
void ug_soundcard_out::ListInterface2(std::vector<class InterfaceObject*>& PList)
{
	// TODO!!! auto number?
	LIST_PIN(L"Out", DR_IN, L"", L"", IO_LINEAR_INPUT|IO_AUTODUPLICATE|IO_RENAME, L"");
}

ug_soundcard_out::ug_soundcard_out()
{
	// if audio driver fails to start. It will try to stop audio.
	// This will crash if it thinks audio fade out is not complete
	assert( m_fade_level == 0.f);
	SetFlag(UGF_POLYPHONIC_AGREGATOR);
}

int ug_soundcard_out::Open()
{
	ug_base::Open();

	SET_PROCESS_FUNC(&ug_base:: process_nothing );

	// Can't oversample Soundcard-out.
	if(dynamic_cast<ug_oversampler*>(AudioMaster()))
	{
		message(L"This module can't be oversampled. Please move it out of oversampler Container.");
		return 0;
	}

	n_channels = 0;

	for( int i = 0 ; i < plugs.size(); i++ )
	{
		if( plugs[i]->HasNonDefaultConnection() )
			n_channels = i + 1;
	}

	// non-registered version limited to two audio outs
#if defined(COMPILE_DEMO_VERSION)
	{
		n_channels = max(n_channels,2);
	}
#endif

	if( n_channels < 1 )
		return 0;

	// start fade up
	SET_PROCESS_FUNC(&ug_soundcard_out::sub_process<true>);
	m_fade_inc = 1.f / (SOUNDCARD_FADE_TIME * getSampleRate());
	m_fade_level = m_fade_inc;

	// will fail if > 1 live output
	return AudioMaster()->RegisterIoModule(this);
}

// rapid fade out to prevent clicks
void ug_soundcard_out::startFade(bool)
{
	m_fade_inc = -1.f / (SOUNDCARD_FADE_TIME * getSampleRate());
	SET_PROCESS_FUNC(&ug_soundcard_out::sub_process<true>);

	if( m_fade_level != 0.f )
	{
		m_fade_level = 1.f + m_fade_inc;
	}
}

void ug_soundcard_out::setIoBuffers(float* const* p_outputs, int numChannels)
{
	driverBuffers.assign(p_outputs, p_outputs + numChannels);
}

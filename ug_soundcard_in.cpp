
#include "ug_soundcard_in.h"
#include "resource.h"
#include "module_register.h"
#include "ug_oversampler.h"

SE_DECLARE_INIT_STATIC_FILE(ug_soundcard_in);

namespace
{
REGISTER_MODULE_1(L"Sound In", IDS_MN_SOUND_IN,IDS_MG_INPUT_OUTPUT,ug_soundcard_in,CF_STRUCTURE_VIEW,L"Gets audio from your mic/line in.  You are limited to one soundcard in module.");
}

// Fill an array of InterfaceObjects with plugs and parameters
void ug_soundcard_in::ListInterface2(std::vector<class InterfaceObject*>& PList)
{
	LIST_PIN(L"In", DR_OUT, L"", L"", IO_AUTODUPLICATE|IO_RENAME, L"");
}

int ug_soundcard_in::Open()
{
	ug_base::Open();

	n_channels = 0;

	// Can't oversample.
	if(dynamic_cast<ug_oversampler*>(AudioMaster()))
	{
		message(L"This module can't be oversampled. Please move it out of oversampler Container.");

		for(auto& p : plugs)
		{
			OutputChange(SampleClock(), p, ST_STATIC);
		}
		return 0;
	}

	for( int i = 0 ; i < plugs.size(); i++ )
	{
		if( plugs[i]->HasNonDefaultConnection() )
			n_channels = i + 1;
	}

	int i;

	for (i = static_cast<int>(plugs.size()) - 1; i >= n_channels; i--)
	{
		OutputChange(SampleClock(), GetPlug(i), ST_STATIC);
	}

	assert(i == n_channels - 1);

	for (; i >= 0; i--)
	{
		OutputChange(SampleClock(), GetPlug(i), ST_RUN);
	}

	if (n_channels > 0)
	{
		SET_PROCESS_FUNC(&ug_soundcard_in::sub_process);
	}

	AudioMaster()->RegisterIoModule(this);

	return 0;
}

void ug_soundcard_in::setIoBuffers(const float* const* p_outputs, int numChannels)
{
	driverBuffers.assign(p_outputs, p_outputs + numChannels);
}
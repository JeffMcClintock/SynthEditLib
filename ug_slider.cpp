#include "ug_slider.h"
#include "resource.h"
#include "module_register.h"

SE_DECLARE_INIT_STATIC_FILE(ug_slider)

namespace
{
REGISTER_MODULE_1(L"Slider", IDS_MN_SLIDER,IDS_MG_CONTROLS,ug_slider ,CF_STRUCTURE_VIEW|CF_PANEL_VIEW,L"Allows direct control of an input voltage.  For example connect one to an Oscillators's 'Pitch' input.  Can appear as a knob or button as well (See 'Appearance' parameter). Can be configured to send any MIDI controller or NRPN number, handy for controlling external MIDI gear.");
}

#define PLG_OUT 6
#define PLG_PATCH_VALUE 12

// Fill an array of InterfaceObjects with plugs and parameters
void ug_slider::ListInterface2(InterfaceObjectArray& PList)
{
	//////////////////////////// ug_control::ListInterface2(PList);
	LIST_VAR3( L"Channel", trash_sample_ptr, DR_IN, DT_ENUM , L"-1", MIDI_CHAN_LIST,IO_DISABLE_IF_POS|IO_IGNORE_PATCH_CHANGE|IO_POLYPHONIC_ACTIVE, L"MIDI Channel");
	// these two retained only as dummy connections to MIDI automator (to ensure correct Sort Order)
	LIST_VAR3N( L"MIDI In",  DR_IN, DT_MIDI2 , L"0", L"", IO_DISABLE_IF_POS, L"");
	LIST_VAR3N(L"MIDI Out", DR_OUT, DT_MIDI2 , L"", L"", IO_DISABLE_IF_POS, L"");
	LIST_VAR3( L"MIDI Controller ID", controller_number, DR_IN, DT_ENUM , L"-1",L"", IO_POLYPHONIC_ACTIVE|IO_HIDE_PIN, L"");
	LIST_VAR3( L"Ignore Program Change", ignore_prog_change, DR_IN, DT_BOOL , L"0", L"", IO_HIDE_PIN, L"");
	LIST_VAR3( L"MIDI NRPN", nrpn, DR_IN, DT_ENUM , L"0", L"range 0,16255",IO_HIDE_PIN, L"");
	////////////////////////////

	LIST_PIN( L"Signal Out", DR_OUT, L"", L"", IO_AUTOCONFIGURE_PARAMETER, L"");
	// obsolete, no longer used. Function transfered to patch memory.
	LIST_VAR3N( L"Lo Value", DR_IN, DT_FLOAT , L"0", L"", IO_HIDE_PIN, L"");
	LIST_VAR3N( L"Hi Value", DR_IN, DT_FLOAT , L"10", L"", IO_HIDE_PIN, L"");
	// if you add a new appearance, check UpdateOutput() handles it
	LIST_VAR3( L"Appearance",appearance,DR_IN,DT_ENUM, L"1", L"None=-1,Plain Slider, Vert Slider, Horiz Slider, Knob, Button, Button (toggle),Plain Button, Small Knob, Small Button, Small Button (Toggle)",IO_MINIMISED, L"Sets the appearance of the control" );
	LIST_VAR3( L"Show Readout",trash_bool_ptr,DR_IN,DT_BOOL, L"1", L"",IO_MINIMISED, L"Adds text readout of value" );
	LIST_VAR3( L"Show Title On Panel", trash_bool_ptr, DR_IN, DT_BOOL , L"1", L"", IO_MINIMISED, L"Leaves control's title blank on Control Panel");
	// SE 1.1
	LIST_VAR3( L"patchValue", patchValue_, DR_IN, DT_FLOAT , L"0", L"", IO_HIDE_PIN|IO_PATCH_STORE, L"");
	LIST_VAR_UI( L"patchValue", DR_OUT, DT_FLOAT , L"", L"", IO_UI_COMMUNICATION|IO_HIDE_PIN|IO_PATCH_STORE, L""); // not counted in dsp
	LIST_VAR_UI( L"Hint", DR_OUT, DT_TEXT , L"", L"", IO_UI_COMMUNICATION|IO_MINIMISED, L""); // not counted in dsp
}

int ug_slider::Open()
{
	auto r = ug_control::Open();

	m_output_ptr = GetPlug(PLG_OUT)->GetSamplePtr();
	smoother.Init(getSampleRate());

	return r;
}

void ug_slider::onSetPin(timestamp_t p_clock, UPlug* p_to_plug, state_type )
{
	if (p_to_plug == GetPlug(PLG_PATCH_VALUE))
	{
		const float output_val = patchValue_ * 0.1f; // Voltages are divided by 10.

		if (appearance == 8 || (appearance > 3 && appearance < 7))	// switch, minimal smoothing
		{
			// allow 4 samples for transition
			smoother.SetTargetWithTimeInSamples(output_val, 4.f);
		}
		else
		{
			smoother.setTarget(output_val);
		}

		ResetStaticOutput();
		GetPlug(PLG_OUT)->setStreamingA(true, p_clock);

		SET_CUR_FUNC(&ug_slider::sub_process);
	}
}

void ug_slider::sub_process(int start_pos, int sampleframes)
{
	auto out = m_output_ptr + start_pos;

	int todo = sampleframes;
	if (smoother.isDone())
	{
		todo = (std::min)(static_output_count, sampleframes);

		static_output_count -= todo;

		if (static_output_count <= 0)
		{
			SET_CUR_FUNC(&ug_base::process_sleep);
			GetPlug(PLG_OUT)->setStreamingA(false, SampleClock());
		}
	}

	for (int s = todo; s > 0; s--)
	{
		*out++ = smoother.getNext();
	}
}


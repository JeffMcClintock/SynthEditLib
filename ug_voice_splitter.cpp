
#include "ug_voice_splitter.h"

#include "SeAudioMaster.h"
#include "resource.h"
#include "module_register.h"

SE_DECLARE_INIT_STATIC_FILE(ug_voice_splitter);

namespace
{
	REGISTER_MODULE_1(L"VoiceSplitter", IDS_MN_VOICE_SPLITTER, IDS_MG_SPECIAL, ug_voice_splitter, CF_STRUCTURE_VIEW, L"");
}

ug_voice_splitter::ug_voice_splitter() :
	voice_(-1)
	, current_output_plug(nullptr)
	, current_output_plug_number(-2)
{
	SET_PROCESS_FUNC(&ug_voice_splitter::sub_process);

	// Prevent downstream modules being polyphonic.
	SetFlag(UGF_POLYPHONIC_DOWNSTREAM|UGF_VOICE_SPLITTER);
}

#define PLG_IN		0
#define PLG_OUT1	1

void ug_voice_splitter::ListInterface2(std::vector<class InterfaceObject*>& PList)
{
	// IO Var, Direction, Datatype, Name, Default, defid (index into ug_base::PlugFormats)
	LIST_PIN2(L"Input", in_ptr, DR_IN, L"0", L"", 0, L"");
	LIST_PIN(L"Output", DR_OUT, L"", L"", IO_AUTODUPLICATE | IO_CUSTOMISABLE, L"");
}

void ug_voice_splitter::onSetPin(timestamp_t p_clock, UPlug* p_to_plug, state_type p_state)
{
	//	_RPT2(_CRT_WARN, "ug_voice_splitter::onSetPin v=%d clock=%d\n", voice_, p_clock );
	if (current_output_plug == 0) // then leave output alone, it's not mine.
		return;

	assert(p_to_plug->getPlugIndex() == PLG_IN);
	OutputChange(p_clock, current_output_plug, p_state);

	if (p_state == ST_RUN)
	{
		SET_PROCESS_FUNC(&ug_voice_splitter::sub_process);
	}
	else
	{
		SET_PROCESS_FUNC(&ug_voice_splitter::sub_process_static);
		ResetStaticOutput();
	}
}

void ug_voice_splitter::sub_process(int start_pos, int sampleframes)
{
	float* in1 = in_ptr + start_pos;
	float* out = out_ptr + start_pos;

	for (int s = sampleframes; s > 0; s--)
	{
		*out++ = *in1++;
	}
}

void ug_voice_splitter::sub_process_static(int start_pos, int sampleframes)
{
	//	sub_process2( start_pos, sampleframes );
	sub_process(start_pos, sampleframes);
	SleepIfOutputStatic(sampleframes);
}

int ug_voice_splitter::Open()
{
	ug_base::Open();
	numOutputs = GetPlugCount() - 1;

	if (pp_voice_num < 1) // monophonic. 0 or -1
	{
		voice_ = 0;
	}
	else
	{
		voice_ = pp_voice_num - 1; // skip voice 0 which is monophonic modules only.
	}

	assert(voice_ == (std::max)(0, pp_voice_num - 1)); // skip voice 0 which is monophonic modules only.

#if 0 //defined( _DEBUG )
	it_ug_clones itr(CloneOf());

	int v = 0;

	for (itr.First(); !itr.IsDone(); itr.Next())
	{
		if (itr.CurrentItem() == this)
		{
			assert(voice_ == v);
			break;
		}

		++v;
	}
#endif

	SET_PROCESS_FUNC(&ug_base::process_sleep);
	OnNewSetting();

	return 0;
}

void ug_voice_splitter::OnNewSetting()
{
	if (numOutputs < 1)
		return;

	// constrain output number to legal range
	int output_number = voice_;

	//output_number = min( output_number, numOutputs - 1 );
	if (output_number >= numOutputs)
	{
		output_number = -1; // hidden output.
	}
	else
	{
		current_output_plug_number = output_number + PLG_OUT1;
		current_output_plug = GetPlug(current_output_plug_number);
		out_ptr = current_output_plug->GetSamplePtr();
	}
}


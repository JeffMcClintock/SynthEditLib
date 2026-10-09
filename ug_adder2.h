// Virtual base class for all unit generators
// (Anything that creates or modifies sound)
#pragma once

#include "ug_base.h"

class ug_adder2 : public ug_base
{
public:
	void sub_process_static(int start_pos, int sampleframes);
	void sub_process(int start_pos, int sampleframes);
	void sub_process_mixed(int start_pos, int sampleframes);
	void onSetPin(timestamp_t p_clock, UPlug* p_to_plug, state_type p_state ) override;
	virtual void NewConnection(UPlug* p_from);

	DECLARE_UG_INFO_FUNC2;
	DECLARE_UG_BUILD_FUNC(ug_adder2);

	ug_adder2();
	~ug_adder2();
	int Open() override;
	bool BypassPin(UPlug* fromPin, UPlug* toPin) override;
	void CacheRunningInputs();

protected:
	float static_input_sum;
	float** running_inputs;
	float* output_ptr;
	int running_input_size;
	int running_input_max_size;
};

// Voice Combiner is simply an Adder.
class ug_voice_combiner :
	public ug_adder2
{
public:
	ug_voice_combiner();
	DECLARE_UG_BUILD_FUNC(ug_voice_combiner);
};

// Poly-to-Mono is similar to Voice-Combiner (but behaves like a switch).
class ug_poly_to_monoB :
	public ug_adder2
{
	int currentActiveVoiceNumber;
	class ug_container* voiceContainer_ = nullptr; // sends us UET_VOICE_FOCUS

public:
	ug_poly_to_monoB() :
		currentActiveVoiceNumber(-1)
	{
		SetFlag(UGF_POLYPHONIC_AGREGATOR| UGF_VOICE_MON_IGNORE);
	}

	void sub_process_static_silence(int start_pos, int sampleframes)
	{
		sub_process_silence(start_pos, sampleframes);
		// mechanism to switch to idle after one block filled staticaly
		SleepIfOutputStatic(sampleframes);
	}

	void sub_process_silence(int start_pos, int sampleframes)
	{
		assert(currentActiveVoiceNumber < 0);
		auto* __restrict out = plugs[0]->GetSamplePtr() + start_pos;

		// auto-vectorized copy.
		while (sampleframes > 3)
		{
			out[0] = 0.0f;
			out[1] = 0.0f;
			out[2] = 0.0f;
			out[3] = 0.0f;

			out += 4;
			sampleframes -= 4;
		}

		while (sampleframes > 0)
		{
			*out++ = 0.0f;
			--sampleframes;
		}
	}

	void sub_process_static(int start_pos, int sampleframes)
	{
		sub_process(start_pos, sampleframes);
		// mechanism to switch to idle after one block filled staticaly
		SleepIfOutputStatic(sampleframes);
	}

	void sub_process(int start_pos, int sampleframes)
	{
		assert(currentActiveVoiceNumber >= 0);
		const float* in = plugs[2 + currentActiveVoiceNumber]->GetSamplePtr() + start_pos;
		float* __restrict out = plugs[0]->GetSamplePtr() + start_pos;

		// auto-vectorized copy.
		while (sampleframes > 3)
		{
			out[0] = in[0];
			out[1] = in[1];
			out[2] = in[2];
			out[3] = in[3];

			out += 4;
			in += 4;
			sampleframes -= 4;
		}

		while (sampleframes > 0)
		{
			*out++ = *in++;
			--sampleframes;
		}
	}

	void OnFocusVoice(int voice, timestamp_t p_clock)
	{
		if (voice == currentActiveVoiceNumber)
			return;

		currentActiveVoiceNumber = voice;
		UpdateOutputStatus(p_clock);
	}

	void UpdateOutputStatus(timestamp_t p_clock)
	{
		bool streaming = currentActiveVoiceNumber >= 0 ? plugs[2 + currentActiveVoiceNumber]->isStreaming() : false;
		if (!streaming || GetPlug(0)->isStreaming() != streaming)
		{
			GetPlug(0)->setStreamingA(streaming, p_clock);
		}

		if (!streaming)
		{
			ResetStaticOutput();
			if (currentActiveVoiceNumber >= 0)
			{
				setSubProcess(&ug_poly_to_monoB::sub_process_static);
			}
			else
			{
				setSubProcess(&ug_poly_to_monoB::sub_process_static_silence);
			}
		}
		else
		{
			if (currentActiveVoiceNumber >= 0)
			{
				setSubProcess(&ug_poly_to_monoB::sub_process);
			}
			else
			{
				setSubProcess(&ug_poly_to_monoB::sub_process_silence);
			}
		}
	}

	void onSetPin(timestamp_t p_clock, UPlug* p_to_plug, state_type /*p_state*/) override
	{
		if (p_to_plug->getPlugIndex() - 2 == currentActiveVoiceNumber)
		{
			//GetPlug(0)->setStreamingA(p_state == ST_RUN, p_clock);
			UpdateOutputStatus(p_clock);
		}
	}

	int Open() override;
	int Close() override;

	void OnFirstSample()
	{
		//GetPlug(0)->setStreamingA(false, 0);
		UpdateOutputStatus(0);
	}

	void HandleEvent(SynthEditEvent* e) override;
};


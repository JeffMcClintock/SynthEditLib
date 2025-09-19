#pragma once

#include "ug_control.h"
#include "..\se_sdk3\smart_audio_pin.h"

class ug_slider : public ug_control
{
public:
	DECLARE_UG_INFO_FUNC2;
	DECLARE_UG_BUILD_FUNC(ug_slider);

	ug_slider() = default;

	int Open() override;
	void sub_process(int start_pos, int sampleframes);
	void onSetPin(timestamp_t p_clock, UPlug* p_to_plug, state_type p_state ) override;

private:
	float* m_output_ptr{};
	float patchValue_{};
	short appearance{};

	RampGeneratorAdaptive smoother;
};

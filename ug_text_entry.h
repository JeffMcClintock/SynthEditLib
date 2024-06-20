#pragma once

#include "ug_control.h"

class ug_text_entry : public ug_control
{
public:
	DECLARE_UG_INFO_FUNC2;
	DECLARE_UG_BUILD_FUNC(ug_text_entry);

	void onSetPin(timestamp_t p_clock, UPlug* p_to_plug, state_type p_state ) override;

private:
	std::wstring patchValue_;
	std::wstring TextOutput;
};

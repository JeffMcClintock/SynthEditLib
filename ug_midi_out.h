#pragma once

#include "ug_base.h"
#include "iseshelldsp.h"

class ug_midi_out : public ug_base, public ISpecialIoModule
{
public:
	DECLARE_UG_INFO_FUNC2;
	DECLARE_UG_BUILD_FUNC(ug_midi_out);

	ug_midi_out();
	virtual void sub_process(int /*start_pos*/, int /*sampleframes*/) {}
	void HandleEvent(SynthEditEvent* e) override;

	int Open() override;

	std::function<void(timestamp_t, int, const unsigned char*)> midiOutCallback;
};

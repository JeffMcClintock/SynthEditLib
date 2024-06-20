#pragma once

#include "ug_base.h"
#include "UMidiBuffer2.h"
#include "cancellation.h"
#include "mp_midi.h"
#include "iseshelldsp.h"

class ug_vst_in : public ug_base, public ISpecialIoModuleAudioIn
{
#ifdef CANCELLATION_TEST_ENABLE2
    float m_phase = 0.f;
#endif

public:
	ug_vst_in();
	~ug_vst_in();
	DECLARE_UG_INFO_FUNC2;
	DECLARE_UG_BUILD_FUNC(ug_vst_in);

	void sub_process(int start_pos, int sampleframes);
	int Open() override;
    void setIoBuffers(const float* const* p_inputs, int numChannels) override;
    void setInputSilent(int input, bool isSilent) override;
	void sendMidi(timestamp_t clock, const gmpi::midi::message_view& msg) override;
	int getAudioInputCount() override;
	bool wantsMidi() override;
	void setMpeMode(int32_t mpemode) override;

private:
	midi_output midi_out;
	const float** m_inputs;
	// MPE support
	gmpi::midi_2_0::FatMpeConverter mpeConverter;
	timestamp_t midiTempClock;
};

#pragma once
#include "ug_base.h"
#include "iseshelldsp.h"

class ug_soundcard_in : public ug_base, public ISpecialIoModuleAudioIn
{
public:
	int Open() override;
	DECLARE_UG_INFO_FUNC2;
	DECLARE_UG_BUILD_FUNC(ug_soundcard_in);

	// ISpecialIoModuleAudioIn
	void setIoBuffers(const float* const* p_outputs, int numChannels) override;
	// not used overrides.
	void sendMidi(timestamp_t, const gmpi::midi::message_view&) override {}
	void setInputSilent(int, bool) override {}
	int getAudioInputCount() override {return n_channels;}
	bool wantsMidi() override { return false; }
	void setMpeMode(int32_t) override {}

	void sub_process(int start_pos, int sampleframes)
	{
		const auto numLiveOutputs = (std::min)(n_channels, (int)driverBuffers.size());

		int i = 0;
		for (; i < numLiveOutputs; ++i)
		{
			auto& src = driverBuffers[i];
			auto* dest = GetPlug(i)->GetSamplePtr() + start_pos;

			std::copy(src, src + sampleframes, dest);
			src += sampleframes;
		}

		// zero any remaining buffers
		for (; i < n_channels; ++i)
		{
			auto* dest = GetPlug(i)->GetSamplePtr() + start_pos;

			std::fill(dest, dest + sampleframes, 0.0f);
		}
	}

private:
	int n_channels;
	std::vector<const float*> driverBuffers;
};

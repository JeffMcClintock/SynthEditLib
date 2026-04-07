#pragma once

#include "ug_base.h"
#include "iseshelldsp.h"

class ug_soundcard_out : public ug_base, public ISpecialIoModuleAudioOut
{
public:
	DECLARE_UG_INFO_FUNC2;
	DECLARE_UG_BUILD_FUNC(ug_soundcard_out);

	ug_soundcard_out();
	int Open() override;

	// ISpecialIoModuleAudioOut
	void startFade(bool isDucked) override;
	void setIoBuffers(float* const* p_outputs, int numChannels) override;
	// unused
	int getAudioOutputCount() override {return n_channels;}
	bool sendsMidi() override { return false; }
	class MidiBuffer3* getMidiOutputBuffer() override { return {}; }
	uint64_t getSilenceFlags(int, int) override { return 0xffffffff; }
	int getOverallPluginLatencySamples() override { return 0;}

	template<bool withFade>
	void sub_process(int start_pos, int sampleframes)
	{
		const auto numLiveOutputs = (std::min)(n_channels, (int)driverBuffers.size());

		float f = m_fade_level;

		int i = 0;
		for (; i < numLiveOutputs; ++i)
		{
			auto& dest = driverBuffers[i];
			const auto* src = GetPlug(i)->GetSamplePtr() + start_pos;

			if constexpr(withFade)
			{
				f = m_fade_level;

				for (int s = 0; s < sampleframes; s++)
				{
					*dest++ = *src++ * f;
					f = std::clamp(f + m_fade_inc, 0.0f, 1.0f);
				}
			}
			else
			{
				std::copy(src, src + sampleframes, dest);
				dest += sampleframes;
			}
		}

		// zero any remaining buffers
		for (; i < (int)driverBuffers.size(); ++i)
		{
			auto& dest = driverBuffers[i];

			std::fill(dest, dest + sampleframes, 0.0f);

			dest += sampleframes;
		}

		if constexpr (withFade)
		{
			m_fade_level = f;

			const bool fadeComplete = m_fade_level == 0.f || m_fade_level == 1.f || numLiveOutputs == 0;

			if (fadeComplete)
			{
				if (m_fade_inc > 0.0f) // fade up done
				{
					SET_PROCESS_FUNC(&ug_soundcard_out::sub_process<false>);
				}
				else // fade down done (stop)
				{
					AudioMaster()->onFadeOutComplete();
				}
			}
		}
	}

private:
	float m_fade_level = 0.0f;
	float m_fade_inc;

	std::vector<float*> driverBuffers;
	int n_channels;
};

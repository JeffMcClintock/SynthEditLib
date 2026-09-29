#pragma once

#include <vector>
#include "ug_oversampler_io.h"
#include "SincFilter.h"
#include "DspFilterJeff.h"

class ug_oversampler_out :
	public ug_oversampler_io
{
	SincFilterCoefs sincTaps;	// audio (the final stage when there are half-band stages)
	SincFilterCoefs gausTaps;	// CV
	std::vector<SincFilterCoefs> halfbandTaps;	// audio, 2x decimating stages ahead of sincTaps
	std::vector<std::vector<float>> halfbandCoefData;
	std::vector<DecimatorCascade> Filters3_;
	std::vector<CascadeFilter2> Filters2_;
	bool firMode;
	// how long it takes the filter to clear to 'all same' after input stops changing.
	int staticSettleSamples = 0;

	struct FirPlan
	{
		int sincTaps;
		int sincFactor;
		std::vector<int> halfbands;	// design index of each 2x stage ahead of the sinc, input rate first
		int gaussTaps;
		int predelay;
		int gaussPredelay;
		int latency;	// including predelay
		int settle;
	};
	FirPlan planFir(int filterSetting) const;

	int calcTapCount2(int filterSetting, int oversampleFactor) const
	{
		assert(filterSetting > 10); // FIR mode.

		int FilterTaps = filterSetting;
		if (filterSetting < 20) // Filter specified by quality. Number of taps increases with oversampling rate. (filterSetting 13->16)
		{
			FilterTaps = (2 << (filterSetting - 8)) * oversampleFactor;
			FilterTaps = (std::max)(4, FilterTaps);
		}
		else
		{
			// double filter taps in 4x sinc mode. Provides better rejection and keeps latency constant in Waves.
			if (oversampleFactor > 3)
			{
				FilterTaps *= 2;
			}
		}

		FilterTaps = ((FilterTaps + 2) & 0xfffffffc); // Nearest factor of 4 to utilise SSE.

		//_RPT2(_CRT_WARN, "filter settting %d FilterTaps %d\n", filterSetting, FilterTaps);
		return FilterTaps;
	}

public:
	DECLARE_UG_BUILD_FUNC(ug_oversampler_out);
	ug_oversampler_out();
	void calcLatency(int poles, int oversampleFactor);
	int Open() override;
	ug_base* Clone(CUGLookupList& UGLookupList) override;

	void OnFirstSample();
	void subProcessDownsample(int start_pos, int sampleframes);
	void subProcessFirFilter(int start_pos, int sampleframes);
	void HandleEvent(SynthEditEvent* e) override;
};




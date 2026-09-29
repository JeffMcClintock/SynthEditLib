#pragma once
#include "ug_oversampler_io.h"

class ug_oversampler_in :
	public ug_oversampler_io
{
	// sinc is quite 'ripply' which can be problematic on control signals, but has much better high freq response on audio.
	typedef UpsamplingInterpolator3 InterpolatorType;
//	typedef UpsamplingInterpolator InterpolatorType; // seems to have quite a low-pass effect

	float* InterpolatorCoefs = {};
	float* InterpolatorCoefs_gaussian = {};
	std::vector<InterpolatorType> interpolators_;
	int settleSampleCount;

	struct Design { int taps; double kaiserBeta; };
	static Design upsamplerDesign(int filterSetting);

public:

	DECLARE_UG_BUILD_FUNC(ug_oversampler_in);

	void calcLatency(int factor);

	void OnFirstSample();
	void HandleEvent(SynthEditEvent* e) override;
	void TransmitInitialPinValues();
	void subProcessUpsample(int start_pos, int sampleframes);
	int Open() override;
	void OnOversamplerResume();
	int calcDelayCompensation() override;
	int calcReportedLatency() override; // must mirror the above - see the .cpp
};




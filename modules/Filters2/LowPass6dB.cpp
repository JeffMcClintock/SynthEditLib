// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
//
// One-pole (6 dB/octave) low-pass filter - i.e. a first-order IIR low-pass, a.k.a.
// a 'leaky integrator'. One real pole (plus a trivial zero at the origin).
// Modernised port of the legacy 'ug_filter_1pole' (ug_filter_1pole_lp.cpp) to the GMPI SDK.
//
// Difference equation:  y[n] = x[n] + l * ( y[n-1] - x[n] )
//   where the pole coefficient  l = exp( -2*pi * Fc / Fs ).
//
// gmpi::FilterBase provides the power-saving 'settling' behaviour: once the
// input goes quiet the output is watched until it stops changing, then the
// output is flagged non-streaming so the host can sleep this module.

#include <algorithm>
#include <cmath>
#define _USE_MATH_DEFINES
#include <math.h>
#include "Helpers/FilterBase.h"

using namespace gmpi;

struct LowPass6dB final : public FilterBase
{
	AudioInPin pinSignal;
	AudioInPin pinPitch;
	AudioOutPin pinOutput;

	// filter state
	float y1n = 0.0f;             // previous output (filter memory)
	float l = 0.0f;               // pole coefficient
	float sampleRate = 44100.0f;  // cached (avoids per-sample host calls)
	float expScale = 0.0f;        // = -2*pi / sampleRate  (pole coeff = exp(expScale * freqHz))

	LowPass6dB()
	{
		addOutputPin(pinOutput); // FilterBase watches this output for silence.
	}

	ReturnCode open(api::IUnknown* phost) override
	{
		auto r = FilterBase::open(phost); // randomises the stability-check phase
		sampleRate = getSampleRate();
		expScale = -2.0f * static_cast<float>(M_PI) / sampleRate;
		return r;
	}

	// Convert a (normalised) pitch sample to the pole coefficient 'l'.
	// Pitch is 1 Volt/Octave; in SynthEdit volts = sample * 10 (1.0 == 10 Volts).
	float computeCoeff(float pitchSample) const
	{
		const float volts = 10.0f * pitchSample;
		float freqHz = 440.0f * std::pow(2.0f, volts - 5.0f);

		// Limit the cutoff to just under Nyquist.
		freqHz = (std::min)(freqHz, sampleRate * 0.495f);

		return std::exp(freqHz * expScale);
	}

	// --- audio processing -------------------------------------------------

	// One method, two specialisations: 'fixed' reuses the block's coefficient 'l';
	// 'modulated' recomputes it per-sample from the audio-rate Pitch input.
	template<bool pitchModulated>
	void subProcess(int sampleFrames)
	{
		doStabilityCheck();

		auto in = getBuffer(pinSignal);
		auto out = getBuffer(pinOutput);
		[[maybe_unused]] auto pitch = getBuffer(pinPitch);

		float lc = l; // fixed cutoff (overwritten each sample when modulated)
		float y = y1n;
		for (int s = sampleFrames; s > 0; --s)
		{
			if constexpr (pitchModulated)
				lc = computeCoeff(*pitch++);

			const float xn = *in++;
			y = xn + lc * (y - xn);
			*out++ = y;
		}
		y1n = y;
	}

	// --- FilterBase hooks -------------------------------------------------

	bool isFilterSettling() override
	{
		// Only the inputs feed energy into the filter. Once they are quiet the
		// output decays to a constant and we can eventually sleep.
		return !pinSignal.isStreaming() && !pinPitch.isStreaming();
	}

	void StabilityCheck() override
	{
		if (!std::isfinite(y1n)) // self-oscillation can blow up
			y1n = 0.0f;
	}

	// --- host callbacks ---------------------------------------------------

	void onGraphStart() override
	{
		// If the input is a steady (non-streaming) level, pre-seed the filter to
		// its steady state so it settles immediately instead of ramping up.
		if (!pinSignal.isStreaming())
			y1n = pinSignal.getValue();

		FilterBase::onGraphStart();
	}

	void onSetPins() override
	{
		// Recompute the (fixed) coefficient when the cutoff changes.
		if (pinPitch.isUpdated() && !pinPitch.isStreaming())
			l = computeCoeff(pinPitch.getValue());

		// Any active input keeps the output active.
		pinOutput.setStreaming(true);

		// Pick the cheapest specialisation for the current pitch mode.
		if (pinPitch.isStreaming())
			setSubProcess(&LowPass6dB::subProcess<true>);
		else
			setSubProcess(&LowPass6dB::subProcess<false>);

		initSettling(); // must be last (FilterBase).
	}
};

namespace
{
auto r = Register<LowPass6dB>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Low Pass (6dB)" name="Low Pass (6dB)" category="SDK Examples">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch" datatype="float" rate="audio" default="0.5"/>
        <Pin name="Output" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");
}

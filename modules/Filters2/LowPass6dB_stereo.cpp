// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
//
// Stereo one-pole (6 dB/octave) low-pass filter.
// Two independent first-order low-passes (Left and Right) sharing one cutoff.
//
// This is the stereo companion to LowPass6dB.cpp, and an example of using
// gmpi::FilterBase with more than one audio output: both outputs are registered
// with addOutputPin(), so the base class only sleeps the module once BOTH
// channels have decayed to silence.

#include <algorithm>
#include <cmath>
#define _USE_MATH_DEFINES
#include <math.h>
#include "Helpers/FilterBase.h"

using namespace gmpi;

struct LowPass6dBStereo final : public FilterBase
{
	AudioInPin pinSignalL;
	AudioInPin pinSignalR;
	AudioInPin pinPitch;
	AudioOutPin pinOutputL;
	AudioOutPin pinOutputR;

	// filter state (one memory per channel; the coefficient is shared)
	float y1nL = 0.0f;
	float y1nR = 0.0f;
	float l = 0.0f;               // pole coefficient
	float sampleRate = 44100.0f;  // cached (avoids per-sample host calls)
	float expScale = 0.0f;        // = -2*pi / sampleRate  (pole coeff = exp(expScale * freqHz))

	LowPass6dBStereo()
	{
		// FilterBase watches both outputs; it sleeps us only when both are silent.
		addOutputPin(pinOutputL);
		addOutputPin(pinOutputR);
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

		auto inL = getBuffer(pinSignalL);
		auto inR = getBuffer(pinSignalR);
		auto outL = getBuffer(pinOutputL);
		auto outR = getBuffer(pinOutputR);
		[[maybe_unused]] auto pitch = getBuffer(pinPitch);

		float lc = l; // fixed cutoff (overwritten each sample when modulated)
		float yL = y1nL;
		float yR = y1nR;
		for (int s = sampleFrames; s > 0; --s)
		{
			if constexpr (pitchModulated)
				lc = computeCoeff(*pitch++);

			const float xL = *inL++;
			const float xR = *inR++;
			yL = xL + lc * (yL - xL);
			yR = xR + lc * (yR - xR);
			*outL++ = yL;
			*outR++ = yR;
		}
		y1nL = yL;
		y1nR = yR;
	}

	// --- FilterBase hooks -------------------------------------------------

	bool isFilterSettling() override
	{
		// Settling only once every input is quiet (neither channel nor the cutoff).
		return !pinSignalL.isStreaming() && !pinSignalR.isStreaming() && !pinPitch.isStreaming();
	}

	void StabilityCheck() override
	{
		if (!std::isfinite(y1nL)) y1nL = 0.0f;
		if (!std::isfinite(y1nR)) y1nR = 0.0f;
	}

	// --- host callbacks ---------------------------------------------------

	void onGraphStart() override
	{
		// Pre-seed each channel to a steady (non-streaming) input so it settles
		// immediately instead of ramping up.
		if (!pinSignalL.isStreaming()) y1nL = pinSignalL.getValue();
		if (!pinSignalR.isStreaming()) y1nR = pinSignalR.getValue();

		FilterBase::onGraphStart();
	}

	void onSetPins() override
	{
		// Recompute the (fixed) coefficient when the cutoff changes.
		if (pinPitch.isUpdated() && !pinPitch.isStreaming())
			l = computeCoeff(pinPitch.getValue());

		// Any active input keeps the outputs active.
		pinOutputL.setStreaming(true);
		pinOutputR.setStreaming(true);

		// Pick the cheapest specialisation for the current pitch mode.
		if (pinPitch.isStreaming())
			setSubProcess(&LowPass6dBStereo::subProcess<true>);
		else
			setSubProcess(&LowPass6dBStereo::subProcess<false>);

		initSettling(); // must be last (FilterBase).
	}
};

namespace
{
auto r = Register<LowPass6dBStereo>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Low Pass (6dB) Stereo" name="Low Pass (6dB) Stereo" category="SDK Examples">
    <Audio>
        <Pin name="Signal L" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Signal R" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch" datatype="float" rate="audio" default="0.5"/>
        <Pin name="Output L" datatype="float" rate="audio" direction="out"/>
        <Pin name="Output R" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");
}

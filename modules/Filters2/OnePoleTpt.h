#pragma once

// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
//
// Shared core for the first-order (6 dB/octave) TPT / zero-delay-feedback
// one-pole filters - see LowPassTpt.cpp and HighPassTpt.cpp.
//
// Built with the Topology-Preserving Transform (Vadim Zavalishin, "The Art of VA
// Filter Design"). The integrator is prewarped with g = tan(pi*fc/Fs), so unlike
// the plain exp() 'Low Pass (6dB)' the cutoff stays accurate right up to Nyquist.
//
// 'isHighPass' selects which single output the derived filter produces. Each
// filter writes ONE output buffer - a high-pass that only ever taps its high
// output shouldn't pay to write a low-pass buffer it never uses.

#include <algorithm>
#include <cmath>
#define _USE_MATH_DEFINES
#include <math.h>
#include "Helpers/FilterBase.h"

template<bool isHighPass>
struct OnePoleTptBase : public gmpi::FilterBase
{
	gmpi::AudioInPin pinSignal;
	gmpi::AudioInPin pinPitch;   // cutoff, 1 Volt/Octave
	gmpi::AudioOutPin pinOutput;

	float s = 0.0f;              // TPT integrator state
	float G = 0.0f;              // g/(1+g) (fixed-cutoff path; recomputed per-sample when modulated)

	// cached environment
	float sampleRate = 44100.0f;
	float piOverSr = 0.0f;       // = pi / sampleRate (the tan() argument scale)

	OnePoleTptBase()
	{
		addOutputPin(pinOutput); // FilterBase watches this single output for silence.
	}

	gmpi::ReturnCode open(gmpi::api::IUnknown* phost) override
	{
		auto r = FilterBase::open(phost); // randomises the stability-check phase
		sampleRate = getSampleRate();
		piOverSr = static_cast<float>(M_PI) / sampleRate;
		return r;
	}

	// Cutoff (1V/oct pitch sample) -> TPT coefficient G = g/(1+g), g = tan(pi*fc/Fs).
	// In SynthEdit volts = sample * 10 (1.0 == 10 Volts).
	float cutoffToCoeff(float pitchSample) const
	{
		const float volts = 10.0f * pitchSample;
		float freqHz = 440.0f * std::pow(2.0f, volts - 5.0f);
		freqHz = (std::min)(freqHz, sampleRate * 0.49f); // keep just under Nyquist
		const float g = std::tan(freqHz * piOverSr);
		return g / (1.0f + g);
	}

	template<bool pitchModulated>
	void subProcess(int sampleFrames)
	{
		doStabilityCheck();

		auto in = getBuffer(pinSignal);
		auto out = getBuffer(pinOutput);
		[[maybe_unused]] auto pitch = getBuffer(pinPitch);

		float Gc = G;
		float st = s;
		for (int i = sampleFrames; i > 0; --i)
		{
			if constexpr (pitchModulated)
				Gc = cutoffToCoeff(*pitch++);

			const float x = *in++;
			const float v = (x - st) * Gc; // input to the integrator
			const float lp = v + st;
			st = lp + v;                   // trapezoidal state update

			if constexpr (isHighPass)
				*out++ = x - lp;
			else
				*out++ = lp;
		}
		s = st;
	}

	// --- FilterBase hooks -------------------------------------------------

	bool isFilterSettling() override
	{
		return !pinSignal.isStreaming() && !pinPitch.isStreaming();
	}

	void StabilityCheck() override
	{
		if (!std::isfinite(s)) s = 0.0f;
	}

	// --- host callbacks ---------------------------------------------------

	void onGraphStart() override
	{
		// At DC the integrator state equals the input, so seeding it lets the
		// filter settle immediately: the low-pass starts at the input level, the
		// high-pass starts at zero.
		if (!pinSignal.isStreaming())
			s = pinSignal.getValue();

		FilterBase::onGraphStart();
	}

	void onSetPins() override
	{
		// Recompute the (fixed) coefficient when the cutoff changes.
		if (pinPitch.isUpdated() && !pinPitch.isStreaming())
			G = cutoffToCoeff(pinPitch.getValue());

		pinOutput.setStreaming(true);

		// Pick the cheapest specialisation for the current cutoff mode.
		if (pinPitch.isStreaming())
			setSubProcess(&OnePoleTptBase::subProcess<true>);
		else
			setSubProcess(&OnePoleTptBase::subProcess<false>);

		initSettling(); // must be last (FilterBase).
	}
};

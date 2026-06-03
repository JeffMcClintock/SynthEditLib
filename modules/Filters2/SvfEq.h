#pragma once

// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
//
// Shared core for the TPT / zero-delay-feedback equaliser filters - see
// LowShelfTpt.cpp, HighShelfTpt.cpp and BellTpt.cpp.
//
// These are the Cytomic 'mixed output' state-variable filters (Andrew Simper,
// "Solving the continuous SVF equations using trapezoidal integration"): the
// same 2-pole TPT SVF core, with each EQ shape produced by a different output
// mix  y = m0*v0 + m1*v1 + m2*v2  (and, for the shelves, a gain-dependent
// prewarp of the cutoff). At 0 dB every shape collapses to y = v0, i.e. a
// perfect bypass.
//
// All three EQ shapes share the per-sample loop below; a derived filter only
// supplies updateEqCoeffs() (run per block, never per sample).

#include <algorithm>
#include <cmath>
#define _USE_MATH_DEFINES
#include <math.h>
#include "Helpers/FilterBase.h"

struct SvfEqBase : public gmpi::FilterBase
{
	gmpi::AudioInPin pinSignal;
	gmpi::AudioInPin pinPitch;   // centre / corner frequency, 1 Volt/Octave
	gmpi::FloatInPin pinGain;    // boost/cut in dB
	gmpi::FloatInPin pinQ;       // Q / bandwidth
	gmpi::AudioOutPin pinOutput;

	// ZDF integrator state.
	float ic1eq = 0.0f, ic2eq = 0.0f;

	// SVF coefficients (recomputed per-sample when the frequency is modulated).
	float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;

	// per-block EQ coefficients (set by updateEqCoeffs).
	float gScale = 1.0f;         // multiplies tan(pi*fc/Fs) (shelves prewarp by sqrt(gain))
	float k = 1.0f;              // damping (= 1/Q, or 1/(Q*A) for the bell)
	float m0 = 1.0f, m1 = 0.0f, m2 = 0.0f; // output mix

	// cached environment
	float sampleRate = 44100.0f;
	float piOverSr = 0.0f;

	SvfEqBase()
	{
		addOutputPin(pinOutput);
	}

	gmpi::ReturnCode open(gmpi::api::IUnknown* phost) override
	{
		auto r = FilterBase::open(phost);
		sampleRate = getSampleRate();
		piOverSr = static_cast<float>(M_PI) / sampleRate;
		return r;
	}

	// dB -> 'A' (so that A*A is the linear gain). A = 10^(dB/40).
	static float dBtoA(float dB) { return std::pow(10.0f, dB * (1.0f / 40.0f)); }

	// pitch (1V/oct) -> raw g = tan(pi*fc/Fs), before the per-shape gScale.
	float cutoffTan(float pitchSample) const
	{
		const float volts = 10.0f * pitchSample;
		float freqHz = 440.0f * std::pow(2.0f, volts - 5.0f);
		freqHz = (std::min)(freqHz, sampleRate * 0.49f);
		return std::tan(freqHz * piOverSr);
	}

	// Derived filters fill gScale, k, m0, m1, m2 from gain (dB) and Q. Called per
	// block only (gain/Q are control-rate), so a virtual here costs nothing per sample.
	virtual void updateEqCoeffs(float gainDb, float Q) = 0;

	// SVF mix coefficients from g (which already includes gScale).
	void updateA(float g) { a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2; }

	template<bool freqModulated>
	void subProcess(int sampleFrames)
	{
		doStabilityCheck();

		auto in = getBuffer(pinSignal);
		auto out = getBuffer(pinOutput);
		[[maybe_unused]] auto pitch = getBuffer(pinPitch);

		float la1 = a1, la2 = a2, la3 = a3;
		const float lk = k, lgs = gScale, lm0 = m0, lm1 = m1, lm2 = m2;
		float s1 = ic1eq, s2 = ic2eq;
		for (int i = sampleFrames; i > 0; --i)
		{
			if constexpr (freqModulated)
			{
				const float g = cutoffTan(*pitch++) * lgs;
				la1 = 1.0f / (1.0f + g * (g + lk)); la2 = g * la1; la3 = g * la2;
			}

			const float v0 = *in++;
			const float v3 = v0 - s2;
			const float v1 = la1 * s1 + la2 * v3;
			const float v2 = s2 + la2 * s1 + la3 * v3;
			s1 = 2.0f * v1 - s1;
			s2 = 2.0f * v2 - s2;

			*out++ = lm0 * v0 + lm1 * v1 + lm2 * v2;
		}
		ic1eq = s1; ic2eq = s2;
	}

	// --- FilterBase hooks -------------------------------------------------

	bool isFilterSettling() override
	{
		return !pinSignal.isStreaming() && !pinPitch.isStreaming();
	}

	void StabilityCheck() override
	{
		if (!std::isfinite(ic1eq)) ic1eq = 0.0f;
		if (!std::isfinite(ic2eq)) ic2eq = 0.0f;
	}

	void onSetPins() override
	{
		const bool freqModulated = pinPitch.isStreaming();

		// Gain / Q are control-rate: refresh the EQ coefficients when they change.
		if (pinGain.isUpdated() || pinQ.isUpdated())
			updateEqCoeffs(pinGain.getValue(), (std::max)(0.025f, pinQ.getValue()));

		// Fixed-frequency SVF coefficients: recompute on any change.
		if (!freqModulated && (pinPitch.isUpdated() || pinGain.isUpdated() || pinQ.isUpdated()))
			updateA(cutoffTan(pinPitch.getValue()) * gScale);

		pinOutput.setStreaming(true);

		if (freqModulated)
			setSubProcess(&SvfEqBase::subProcess<true>);
		else
			setSubProcess(&SvfEqBase::subProcess<false>);

		initSettling(); // must be last (FilterBase).
	}
};

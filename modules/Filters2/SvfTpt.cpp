// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
//
// State Variable Filter, TPT / zero-delay-feedback form.
//
// A 2-pole (12 dB/octave) multimode filter built with the Topology-Preserving
// Transform (Vadim Zavalishin, "The Art of VA Filter Design"; ZDF solution after
// Andrew Simper / Cytomic). The trapezoidal integrators are prewarped with
// g = tan(pi*fc/Fs) so the cutoff stays accurate right up to Nyquist, and the
// delay-free resonance loop is solved algebraically each sample (so it stays
// stable and in-tune at any cutoff/resonance, even under fast modulation).
//
// It emits Low-, Band- and High-pass simultaneously - an example of using
// gmpi::FilterBase with several audio outputs: all three are registered, so the
// module only sleeps once every output has decayed to silence.

#include <algorithm>
#include <cmath>
#define _USE_MATH_DEFINES
#include <math.h>
#include "Helpers/FilterBase.h"

using namespace gmpi;

struct SvfTpt final : public FilterBase
{
	AudioInPin pinSignal;
	AudioInPin pinPitch;       // cutoff, 1 Volt/Octave
	FloatInPin pinResonance;   // 0 (none) .. 1 (self-oscillation)
	AudioOutPin pinLow;
	AudioOutPin pinBand;
	AudioOutPin pinHigh;

	// ZDF integrator state (Cytomic's two 'equivalent currents').
	float ic1eq = 0.0f;
	float ic2eq = 0.0f;

	// mix coefficients (valid for the fixed-cutoff path; recomputed per-sample
	// when the cutoff is audio-modulated).
	float a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
	float k = 2.0f;            // damping (= 1/Q): 2 = no resonance, 0 = self-oscillation

	// cached environment
	float sampleRate = 44100.0f;
	float piOverSr = 0.0f;     // = pi / sampleRate (the tan() argument scale)

	SvfTpt()
	{
		// FilterBase watches all three outputs; it sleeps us only once all are silent.
		addOutputPin(pinLow);
		addOutputPin(pinBand);
		addOutputPin(pinHigh);
	}

	ReturnCode open(api::IUnknown* phost) override
	{
		auto r = FilterBase::open(phost); // randomises the stability-check phase
		sampleRate = getSampleRate();
		piOverSr = static_cast<float>(M_PI) / sampleRate;
		return r;
	}

	// Cutoff (1V/oct pitch sample) -> prewarped TPT coefficient g = tan(pi*fc/Fs).
	// In SynthEdit volts = sample * 10 (1.0 == 10 Volts).
	float cutoffToG(float pitchSample) const
	{
		const float volts = 10.0f * pitchSample;
		float freqHz = 440.0f * std::pow(2.0f, volts - 5.0f);
		freqHz = (std::min)(freqHz, sampleRate * 0.49f); // keep just under Nyquist
		return std::tan(freqHz * piOverSr);
	}

	// Resonance 0..1 -> damping k (= 1/Q). 0 -> k=2 (no peak); 1 -> k=0 (self-osc).
	static float resonanceToK(float resonance)
	{
		return 2.0f * (1.0f - std::clamp(resonance, 0.0f, 1.0f));
	}

	// Derive the ZDF mix coefficients from g (using the current damping k).
	void updateCoeffs(float g)
	{
		a1 = 1.0f / (1.0f + g * (g + k));
		a2 = g * a1;
		a3 = g * a2;
	}

	// --- audio processing -------------------------------------------------

	template<bool pitchModulated>
	void subProcess(int sampleFrames)
	{
		doStabilityCheck();

		auto in = getBuffer(pinSignal);
		auto low = getBuffer(pinLow);
		auto band = getBuffer(pinBand);
		auto high = getBuffer(pinHigh);
		[[maybe_unused]] auto pitch = getBuffer(pinPitch);

		float la1 = a1, la2 = a2, la3 = a3;
		const float lk = k;
		float s1 = ic1eq, s2 = ic2eq;
		for (int s = sampleFrames; s > 0; --s)
		{
			if constexpr (pitchModulated)
			{
				const float g = cutoffToG(*pitch++);
				la1 = 1.0f / (1.0f + g * (g + lk));
				la2 = g * la1;
				la3 = g * la2;
			}

			const float v0 = *in++;
			const float v3 = v0 - s2;
			const float v1 = la1 * s1 + la2 * v3;   // band-pass
			const float v2 = s2 + la2 * s1 + la3 * v3; // low-pass
			s1 = 2.0f * v1 - s1;
			s2 = 2.0f * v2 - s2;

			*low++  = v2;
			*band++ = v1;
			*high++ = v0 - lk * v1 - v2;             // high-pass
		}
		ic1eq = s1;
		ic2eq = s2;
	}

	// --- FilterBase hooks -------------------------------------------------

	bool isFilterSettling() override
	{
		// Resonance is control-rate, so only the audio inputs feed the filter.
		return !pinSignal.isStreaming() && !pinPitch.isStreaming();
	}

	void StabilityCheck() override
	{
		if (!std::isfinite(ic1eq)) ic1eq = 0.0f;
		if (!std::isfinite(ic2eq)) ic2eq = 0.0f;
	}

	// --- host callbacks ---------------------------------------------------

	void onSetPins() override
	{
		const bool pitchModulated = pinPitch.isStreaming();

		// Resonance is control-rate: keep the damping current.
		if (pinResonance.isUpdated())
			k = resonanceToK(pinResonance.getValue());

		// Fixed-cutoff coefficients: recompute when cutoff or resonance changed.
		// (When the cutoff is audio-modulated they are recomputed per-sample instead.)
		if (!pitchModulated && (pinPitch.isUpdated() || pinResonance.isUpdated()))
			updateCoeffs(cutoffToG(pinPitch.getValue()));

		// Any active input keeps the outputs active.
		pinLow.setStreaming(true);
		pinBand.setStreaming(true);
		pinHigh.setStreaming(true);

		// Pick the cheapest specialisation for the current cutoff mode.
		if (pitchModulated)
			setSubProcess(&SvfTpt::subProcess<true>);
		else
			setSubProcess(&SvfTpt::subProcess<false>);

		initSettling(); // must be last (FilterBase).
	}
};

namespace
{
auto r = Register<SvfTpt>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE SVF (TPT)" name="SVF (TPT)" category="SDK Examples">
    <Audio>
        <Pin name="Signal" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Pitch" datatype="float" rate="audio" default="0.5"/>
        <Pin name="Resonance" datatype="float" default="0"/>
        <Pin name="Low Pass" datatype="float" rate="audio" direction="out"/>
        <Pin name="Band Pass" datatype="float" rate="audio" direction="out"/>
        <Pin name="High Pass" datatype="float" rate="audio" direction="out"/>
    </Audio>
</Plugin>
)XML");
}

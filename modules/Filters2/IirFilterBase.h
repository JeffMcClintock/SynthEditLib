#pragma once

// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
//
// Shared machinery for the classic-design IIR filter modules (Butterworth,
// Chebyshev I/II, RBJ) built on the iir1 library - the maintained successor
// of the 'DspFilters' library that the deprecated 'Butterworth ... 2' modules
// use. See iir1/README-SynthEdit.md for provenance.
//
// These are fixed-design filters: the cutoff is a control-rate value in Hz
// (not a 1V/Oct audio signal), and recalculating a design is too expensive
// (and too glitchy) to do per-sample. Instead, when a parameter changes the
// module sets up the new design on a spare filter instance and fades across
// to it: first the input is faded up into the new filter (so its start-up
// transient dies away quietly), then the output is crossfaded from the old
// design to the new one - 50 ms per phase.
// Ported from the gen-2 design (modules/Filters/IIR_Filters2.cpp).

#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
#include "Helpers/FilterBase.h"
#include "iir1/Iir.h"

namespace iir_filters
{

constexpr int maxPoles = 12;

template <typename FilterT>
struct IirFilterBase : public gmpi::FilterBase
{
	// Every module starts with these two pins. The remaining pins
	// (Width/Gain/Ripple/Poles/Output...) are declared by the concrete module:
	// GMPI pins register in member-construction order, so the derived class's
	// pins land after these, keeping each module's pin order identical to its XML.
	gmpi::AudioInPin pinSignal;
	gmpi::FloatInPin pinPitch;	// cutoff (or band centre), Hz

	FilterT filter[2];
	FilterT* currentFilter = {};
	FilterT* nextFilter = {};

	// -1..0: input ramping up into the new filter; 0..1: output crossfading old->new; 1: done.
	float crossfade = -1.0f;
	float fade_inc = {};
	bool filterUpdated = {};
	float lastOut = {};

	// The concrete module owns these pins (see pin-order note above).
	// polesPin stays null for fixed-order designs (RBJ).
	gmpi::EnumInPin* polesPin = {};
	gmpi::AudioOutPin* outputPin = {};

	void initFilter(gmpi::AudioOutPin& output)
	{
		outputPin = &output;
		addOutputPin(output); // FilterBase watches it for silence.
	}

	void initFilter(gmpi::EnumInPin& poles, gmpi::AudioOutPin& output)
	{
		polesPin = &poles;
		initFilter(output);
	}

	// Configure 'f' from the current pin values, e.g. f.setupN(poles(), normalisedPitch()).
	virtual void setupFilterDesign(FilterT& f) = 0;

	// --- pin values clamped to ranges the filter design accepts ------------

	int poles() const
	{
		return std::clamp(polesPin->getValue(), 1, maxPoles);
	}

	float normalisedPitch() const
	{
		constexpr float minPitch = 0.00001f;	// almost zero
		constexpr float maxPitch = 0.499f;	// nyquist-ish
		return std::clamp(pinPitch.getValue() / getSampleRate(), minPitch, maxPitch);
	}

	float normalisedWidth(float widthHz) const
	{
		constexpr float maxWidth = 0.499f;
		return (std::min)(maxWidth, (std::max)(0.01f, widthHz) / getSampleRate());
	}

	// Inputs are clamped before setup, so iir1 should never throw; this backstop
	// keeps the previous coefficients rather than letting an exception escape
	// into the audio thread.
	void setupFilter(FilterT& f)
	{
		try
		{
			setupFilterDesign(f);
		}
		catch (const std::invalid_argument&)
		{
			assert(false); // a parameter clamp is missing
		}
	}

	// --- audio processing ---------------------------------------------------

	template<bool isCrossfading>
	void subProcess(int sampleFrames)
	{
		doStabilityCheck();

		const float* signal = getBuffer(pinSignal);
		float* output = getBuffer(*outputPin);

		if constexpr (isCrossfading)
		{
			if (crossfade >= 1.0f)
			{
				std::swap(nextFilter, currentFilter);
				setSubProcess(&IirFilterBase::subProcess<false>); // done crossfading
				initSettling(); // re-enable sleep monitoring
				subProcess<false>(sampleFrames);
				return;
			}
		}
		else
		{
			if (filterUpdated)
			{
				filterUpdated = false;
				crossfade = -1.0f;
				setupFilter(*nextFilter);
				nextFilter->reset(); // clear any old state that might cause instability

				setSubProcess(&IirFilterBase::subProcess<true>); // start crossfading
				subProcess<true>(sampleFrames);
				return;
			}
		}

		if constexpr (isCrossfading)
		{
			float xf = crossfade;
			for (int s = sampleFrames; s > 0; --s)
			{
				xf = (std::min)(1.0f, xf + fade_inc);
				const float fadeUp = (std::min)(1.0f, xf + 1.0f);	// input ramp into the new filter
				const float mix = (std::max)(0.0f, xf);				// output crossfade

				const float x = *signal++;
				const float yOld = currentFilter->filter(x);
				const float yNew = nextFilter->filter(x * fadeUp);
				*output++ = yOld + mix * (yNew - yOld);
			}
			crossfade = xf;
		}
		else
		{
			for (int s = sampleFrames; s > 0; --s)
			{
				*output++ = currentFilter->filter(*signal++);
			}
		}

		if (sampleFrames > 0)
			lastOut = output[-1];
	}

	// --- FilterBase hooks ---------------------------------------------------

	bool isFilterSettling() override
	{
		// Pitch and the other parameters are control-rate; only the signal feeds energy in.
		return !pinSignal.isStreaming();
	}

	void StabilityCheck() override
	{
		// iir1 keeps its delay lines private, so watch the output instead: it is a
		// linear function of state and input, so a non-finite state always shows up
		// here. Recover by clearing the delay lines.
		if (!std::isfinite(lastOut))
		{
			for (auto& f : filter)
				f.reset();
			lastOut = 0.0f;
		}
	}

	// --- host callbacks -----------------------------------------------------

	gmpi::ReturnCode open(gmpi::api::IUnknown* phost) override
	{
		auto r = FilterBase::open(phost); // randomises the stability-check phase
		fade_inc = 1.0f / (0.05f * getSampleRate()); // 50 ms per crossfade phase
		return r;
	}

	void onGraphStart() override
	{
		FilterBase::onGraphStart();

		setupFilter(filter[0]);
		setupFilter(filter[1]);

		nextFilter = &filter[0];
		currentFilter = &filter[1];

		setSubProcess(&IirFilterBase::subProcess<false>);
		outputPin->setStreaming(true);
	}

	// Concrete modules with Width/Gain/Ripple pins override this to extend the change check.
	virtual bool designParamsUpdated()
	{
		return pinPitch.isUpdated() || (polesPin && polesPin->isUpdated());
	}

	void onSetPins() override
	{
		if (designParamsUpdated())
			filterUpdated = true;

		// updating any input potentially changes the output signal.
		outputPin->setStreaming(true);
		setSubProcess(&IirFilterBase::subProcess<false>);

		initSettling(); // must be last (FilterBase).
	}
};

} // namespace iir_filters

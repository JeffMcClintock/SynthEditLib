// SPDX-License-Identifier: ISC
// Copyright 2007-2026 Jeff McClintock.
//
// Delay3 - an echo effect: a modernised 'Delay2' (ug_delay).
//
// The 'Modulation' input scales the delay time: 10 Volts = the full 'Delay Time (secs)',
// 0 Volts = zero delay. 'Feedback' recirculates the delayed signal into the line (via the
// previous sample, so the feedback loop is one sample longer than the delay - same as
// Delay2).
//
// Deliberate fixes over Delay2:
// - Exact modulation calibration: volts scale the requested delay itself (sampleRate *
//   'Delay Time', kept as an un-rounded double); only the memory allocation is rounded
//   (up). Delay2 scaled modulation by the truncated integer line length, so every
//   modulated delay carried a proportional error of up to a whole sample - an audible
//   mistuning on short (flanger / Karplus-Strong) delays, varying with sample rate.
// - The 'Interpolate Output' bool became an 'Interpolation' choice - None, Linear,
//   Cubic, Sinc (the interpolator set of Sample Oscillator2; Sinc is its 8-tap,
//   32-phase windowed-sinc filter bank) - so the end-user picks the trade-off between
//   fidelity, CPU, and the shortest reachable feedback loop. The read-offset maths is
//   double precision (float quantised the sub-sample position to ~0.03 samples on a
//   10-second line).
// - A constant 4-sample pre-delay, declared as latency (the XML 'latency' attribute, so
//   it is known at build time - no runtime re-report, no restart). An interpolation
//   kernel needs samples NEWER than the read position (Sinc 4, Cubic 2, Linear 1),
//   which would otherwise put that floor under the modulated delay; with the pre-delay
//   reported and erased by delay compensation, the effective delay sweeps all the way
//   to zero in every mode. (The pre-delay is the Sinc worst case so the report can stay
//   constant while the user switches modes live.)
//   The feedback tap deliberately does NOT include the pre-delay - recirculating the
//   pre-delayed output would stretch the echo loop to delay+5 and no host compensation
//   can reach inside a feedback loop. (With compensation disabled everything simply
//   arrives 4 samples late, echo spacing still exact.) The only residue: the feedback
//   tap itself is causality-limited by the kernel, so the loop bottoms out at
//   1 / 2 / 3 / 5 samples for None / Linear / Cubic / Sinc - another axis of the same
//   user-chosen trade-off.

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <numbers>
#include <vector>
#include "Processor.h"

using namespace gmpi;

namespace
{
// Windowed-sinc interpolator (per Sample Oscillator2): an 8-tap FIR with 32 sub-sample
// phases; the two nearest phases are blended for a continuous fractional delay.
constexpr int sincTaps = 8;
constexpr int sincPhases = 32;
constexpr int sincCenter = 3; // taps span [-sincCenter, +4] around the read position

enum class InterpMode { none, linear, cubic, sinc }; // pin metadata order
enum class FbMode { off, fixed, modulated };

// Samples the kernel reads behind / ahead of the integer read position.
constexpr int pastTaps(InterpMode m)
{
	return m == InterpMode::sinc ? sincCenter : (m == InterpMode::cubic ? 1 : 0);
}
constexpr int futureTaps(InterpMode m)
{
	switch (m)
	{
	case InterpMode::linear: return 1;
	case InterpMode::cubic:  return 2;
	case InterpMode::sinc:   return sincTaps - 1 - sincCenter;
	default:                 return 0;
	}
}

// The constant pre-delay on the output tap: the 'newer' samples the widest kernel
// (Sinc) needs. Reported to the host as latency - MUST match latency="4" in the XML
// below. Constant across interpolation modes, so the report never changes at runtime.
constexpr int preDelaySamples = futureTaps(InterpMode::sinc);

// Forward overrun margin: a read starting anywhere in the line may run up to
// (sincTaps - 1) cells past the end; those cells mirror the line's first cells.
constexpr int mirrorSamples = sincTaps - 1;
}

struct Delay3 final : public Processor
{
	// Pins register in declaration order, which must match the XML below.
	AudioInPin pinSignal;
	AudioInPin pinModulation;
	AudioOutPin pinOutput;
	FloatInPin pinDelayTime;
	EnumInPin pinInterpolation;
	AudioInPin pinFeedback;

	std::vector<float> buffer;    // the delay line, plus the mirrored 'off-end' samples
	double delaySamplesExact = 0; // sampleRate * 'Delay Time', un-rounded: the modulation scale
	int bufferSize = 1;           // ceil(delaySamplesExact): the delay at full (10V) modulation
	int paddedSize = 1;           // line length; the extra cells hold the pre-delay + kernel margin
	int writeIndex = 0;
	float prevFb = 0.0f;          // previous feedback-tap sample (the loop's one-sample element)

	SubProcessPtr actualSubProcess{}; // the real process function, while subProcessDrain() wraps it
	int drainCount = 0;

	ReturnCode open(api::IUnknown* phost) override
	{
		const auto r = Processor::open(phost);
		createBuffer(); // pins haven't transmitted yet: a minimal safety net until they do.
		return r;
	}

	void createBuffer()
	{
		const double sampleRate = host->getSampleRate();

		// The modulation scale keeps the full precision of the request; rounding is
		// confined to the allocation below. Limited to maximum 10s.
		delaySamplesExact = std::clamp(sampleRate * pinDelayTime.getValue(), 0.0, sampleRate * 10.0);

		// Round the line length UP so full modulation can still reach delaySamplesExact.
		bufferSize = (std::max)(static_cast<int>(std::ceil(delaySamplesExact)), 1);

		// +8: 4 cells of pre-delay headroom plus the 4 'older' cells the widest kernel
		// reads behind the output tap - at full modulation the output tap sits at
		// physical delay bufferSize + 4 with its whole kernel inside the line.
		paddedSize = bufferSize + preDelaySamples + 1 + sincCenter;
		buffer.assign(paddedSize + mirrorSamples, 0.0f);

		writeIndex = 0;
		resetDrainCounter();
	}

	// Continuous read offset -> clamped integer + fraction. The clamps keep every kernel
	// tap inside the line.
	template<InterpMode mode>
	void toTap(double offset, int& readOffset, float& readOffsetFine) const
	{
		readOffset = static_cast<int>(offset); // truncate toward zero
		readOffsetFine = static_cast<float>(offset - readOffset);

		constexpr int minOffset = 1 + pastTaps(mode);
		const int maxOffset = paddedSize - futureTaps(mode);

		if (readOffset < minOffset)
		{
			readOffset = minOffset;
			readOffsetFine = 0.0f;
		}
		else if (readOffset > maxOffset)
		{
			readOffset = maxOffset;
			readOffsetFine = 0.0f;
		}
	}

	// Modulation sample -> the two tap positions. The delay is modulation *
	// delaySamplesExact: the volts->seconds calibration is exact, and the interpolator
	// realises the fractional sample. The output tap adds the reported pre-delay; the
	// feedback tap does not, so the loop time stays delay + 1 exactly as in Delay2.
	template<InterpMode mode, FbMode fbMode>
	void calculateReadOffsets(float modulation, int& offsetOut, float& fineOut,
		int& offsetFb, float& fineFb) const
	{
		const double delaySamples = modulation * delaySamplesExact; // 1.0 (10 Volts) = the full 'Delay Time'

		toTap<mode>(paddedSize - preDelaySamples - delaySamples, offsetOut, fineOut);

		if constexpr (fbMode != FbMode::off)
			toTap<mode>(paddedSize - delaySamples, offsetFb, fineFb);
	}

	// The 33 * 8 filter bank, generated as in Sample Oscillator2's GetInterpolationtable():
	// windowed sinc (Hann-squared taper), each phase normalised to unity gain, plus a 33rd
	// phase (the first shifted one tap) so phase blending can always read 'one past'.
	static const std::array<float, (sincPhases + 1) * sincTaps>& sincTable()
	{
		static const auto table = []
		{
			std::array<float, (sincPhases + 1) * sincTaps> t{};
			constexpr int width = sincTaps / 2;
			constexpr double pi = std::numbers::pi;

			for (int phase = 0; phase < sincPhases; ++phase)
			{
				const int tableIndex = phase * sincTaps + width - 1;
				for (int x = -width; x < width; ++x)
				{
					const int i = phase + x * sincPhases; // position on the x axis, in 1/32nds of a sample
					const double o = static_cast<double>(i) / sincPhases;
					const double sinc = (i == 0) ? 1.0 : std::sin(pi * o) / (pi * o);
					const double hanning = std::cos(0.5 * pi * i / (sincPhases * width));
					t[tableIndex - x] = static_cast<float>(sinc * hanning * hanning);
				}
			}

			// 33rd phase = the first, shifted one tap (fraction 1.0 == the next sample).
			const int last = sincPhases * sincTaps;
			t[last] = 0.0f;
			for (int k = 1; k < sincTaps; ++k)
				t[last + k] = t[k - 1];

			// Normalise every phase to unity gain, else the slightly-different gains of
			// the sub-filters modulate the signal (audible as 'overtones').
			for (int phase = 0; phase <= sincPhases; ++phase)
			{
				double firSum = 0.0;
				for (int k = 0; k < sincTaps; ++k)
					firSum += t[phase * sincTaps + k];

				for (int k = 0; k < sincTaps; ++k)
				{
					float adjusted = static_cast<float>(t[phase * sincTaps + k] / firSum);
					if (std::fpclassify(adjusted) == FP_SUBNORMAL)
						adjusted = 0.0f;
					t[phase * sincTaps + k] = adjusted;
				}
			}

			return t;
		}();

		return table;
	}

	// The line's value at (taps[sincCenter] + fraction): run the two nearest sub-sample
	// phase filters over the same 8 samples and blend.
	static float sincInterpolate(const float* taps, float fraction)
	{
		const auto& table = sincTable();

		const float phase = fraction * sincPhases;
		const int phaseIndex = static_cast<int>(phase);
		const float phaseFrac = phase - static_cast<float>(phaseIndex);

		const float* coef0 = table.data() + phaseIndex * sincTaps;
		const float* coef1 = coef0 + sincTaps;

		float a0 = 0.0f;
		float a1 = 0.0f;
		for (int k = 0; k < sincTaps; ++k)
		{
			a0 += taps[k] * coef0[k];
			a1 += taps[k] * coef1[k];
		}

		return a0 + phaseFrac * (a1 - a0);
	}

	// Read the line at (writeIndex + readOffset + fine).
	template<InterpMode mode>
	float readTap(int readOffset, float readOffsetFine) const
	{
		int tapBase = writeIndex + readOffset - pastTaps(mode);
		if (tapBase >= paddedSize)
			tapBase -= paddedSize;

		assert(tapBase >= 0
			&& tapBase + pastTaps(mode) + 1 + futureTaps(mode) <= static_cast<int>(buffer.size()));

		const float* y = buffer.data() + tapBase; // y[pastTaps] is the integer read position

		if constexpr (mode == InterpMode::sinc)
		{
			return sincInterpolate(y, readOffsetFine);
		}
		else if constexpr (mode == InterpMode::cubic)
		{
			// 4-point cubic, per Sample Oscillator2's CubicInterpolator.
			const float x = readOffsetFine;
			return y[1] + 0.5f * x * (y[2] - y[0]
				+ x * (2.0f * y[0] - 5.0f * y[1] + 4.0f * y[2] - y[3]
					+ x * (3.0f * (y[1] - y[2]) + y[3] - y[0])));
		}
		else if constexpr (mode == InterpMode::linear)
		{
			return y[0] + readOffsetFine * (y[1] - y[0]);
		}
		else
		{
			return y[0];
		}
	}

	template<bool modulated, InterpMode mode, FbMode fbMode>
	void subProcess(int sampleFrames)
	{
		auto signalIn   = getBuffer(pinSignal);
		auto modulation = getBuffer(pinModulation);
		auto feedback   = getBuffer(pinFeedback);
		auto signalOut  = getBuffer(pinOutput);

		[[maybe_unused]] const float fixedFeedback = *feedback;

		int offsetOut{}, offsetFb{};
		float fineOut{}, fineFb{};
		if constexpr (!modulated)
			calculateReadOffsets<mode, fbMode>(*modulation, offsetOut, fineOut, offsetFb, fineFb);

		for (int s = sampleFrames; s > 0; --s)
		{
			if constexpr (modulated)
				calculateReadOffsets<mode, fbMode>(*modulation++, offsetOut, fineOut, offsetFb, fineFb);

			float inSample = *signalIn++;
			if constexpr (fbMode == FbMode::modulated)
				inSample += prevFb * *feedback++;
			else if constexpr (fbMode == FbMode::fixed)
				inSample += prevFb * fixedFeedback;

			buffer[writeIndex] = inSample;

			// mirror the first cells 'off the end' too, so reads never wrap mid-kernel.
			if (writeIndex < mirrorSamples)
				buffer[writeIndex + paddedSize] = inSample;

			*signalOut++ = readTap<mode>(offsetOut, fineOut);

			if constexpr (fbMode != FbMode::off)
				prevFb = readTap<mode>(offsetFb, fineFb);

			if (++writeIndex == paddedSize)
				writeIndex = 0;
		}
	}

	void resetDrainCounter()
	{
		drainCount = paddedSize + host->getBlockSize();
	}

	// After the input stops, keep processing until the line has fully drained: a whole
	// line-length (plus a block) of pure silence at the output. A briefer settle check
	// isn't safe here - with feedback, successive echoes arrive up to a full line apart.
	void subProcessDrain(int sampleFrames)
	{
		(this->*actualSubProcess)(sampleFrames);

		const float* out = getBuffer(pinOutput);
		if (std::any_of(out, out + sampleFrames, [](float v) { return v != 0.0f; }))
		{
			resetDrainCounter(); // still audible: restart the countdown.
			return;
		}

		drainCount -= sampleFrames;
		if (drainCount <= 0)
		{
			setSubProcess(actualSubProcess); // keeps emitting (zeros) until the host sleeps us.

			// line fully drained. Inside a subProcess the exact event position is unknowable,
			// so pass the segment start explicitly (this whole segment is zeros anyway).
			pinOutput.setStreaming(false, getBlockPosition());
		}
	}

	void onSetPins() override
	{
		if (pinDelayTime.isUpdated())
			createBuffer();

		// Pick the cheapest specialisation for the current pin states. Feedback earns a
		// dedicated 'off' state because a live loop costs a second tap read per sample.
		using Sub = void (Delay3::*)(int);
		constexpr auto N = InterpMode::none, L = InterpMode::linear, C = InterpMode::cubic, S = InterpMode::sinc;
		constexpr auto OFF = FbMode::off, FIX = FbMode::fixed, MOD = FbMode::modulated;

		static constexpr Sub dispatch[2][4][3] = // [modulated][interpolation][fbMode]
		{
			{ { &Delay3::subProcess<false, N, OFF>, &Delay3::subProcess<false, N, FIX>, &Delay3::subProcess<false, N, MOD> },
			  { &Delay3::subProcess<false, L, OFF>, &Delay3::subProcess<false, L, FIX>, &Delay3::subProcess<false, L, MOD> },
			  { &Delay3::subProcess<false, C, OFF>, &Delay3::subProcess<false, C, FIX>, &Delay3::subProcess<false, C, MOD> },
			  { &Delay3::subProcess<false, S, OFF>, &Delay3::subProcess<false, S, FIX>, &Delay3::subProcess<false, S, MOD> } },
			{ { &Delay3::subProcess<true,  N, OFF>, &Delay3::subProcess<true,  N, FIX>, &Delay3::subProcess<true,  N, MOD> },
			  { &Delay3::subProcess<true,  L, OFF>, &Delay3::subProcess<true,  L, FIX>, &Delay3::subProcess<true,  L, MOD> },
			  { &Delay3::subProcess<true,  C, OFF>, &Delay3::subProcess<true,  C, FIX>, &Delay3::subProcess<true,  C, MOD> },
			  { &Delay3::subProcess<true,  S, OFF>, &Delay3::subProcess<true,  S, FIX>, &Delay3::subProcess<true,  S, MOD> } },
		};

		const int interpolation = std::clamp(pinInterpolation.getValue(), 0, 3);
		const int fbMode = pinFeedback.isStreaming() ? 2 : (pinFeedback.getValue() != 0.0f ? 1 : 0);

		actualSubProcess = static_cast<SubProcessPtr>(
			dispatch[pinModulation.isStreaming()][interpolation][fbMode]);

		pinOutput.setStreaming(true); // running - or still draining.

		if (pinSignal.isStreaming())
		{
			setSubProcess(actualSubProcess);
		}
		else
		{
			resetDrainCounter();
			setSubProcess(&Delay3::subProcessDrain);
		}
	}
};

namespace
{
auto r = Register<Delay3>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Delay3" name="Delay3" category="Effects">
    <Audio latency="4">
        <Pin name="Signal In" datatype="float" rate="audio" linearInput="true"/>
        <Pin name="Modulation" datatype="float" rate="audio" default="1"/>
        <Pin name="Signal Out" datatype="float" rate="audio" direction="out"/>
        <Pin name="Delay Time (secs)" datatype="float" default="1" isMinimised="true"/>
        <Pin name="Interpolation" datatype="enum" default="0" metadata="None,Linear,Cubic,Sinc" isMinimised="true"/>
        <Pin name="Feedback" datatype="float" rate="audio"/>
    </Audio>
</Plugin>
)XML");
}

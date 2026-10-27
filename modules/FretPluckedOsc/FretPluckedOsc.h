#pragma once

#include <cmath>
#include <memory>
#include "Processor.h"
#include "FretPluckedCache.h"

#ifndef SE_DECLARE_INIT_STATIC_FILE
#define SE_DECLARE_INIT_STATIC_FILE(filename) void se_static_library_init_##filename(){}
#endif

// Plays a baked plucked-string multisample, one sample per semitone, through a built-in VCA.
// The VCA lives here because only the oscillator knows when playback has reached the loop.
class FretPluckedOsc : public gmpi::Processor
{
	enum class State { Idle, Playing, Released };

	float sampleRate_ = 44100.0f;

	std::shared_ptr<fret_plucked::BakeLoader> loader_;
	std::shared_ptr<const fret_plucked::Table> table_;      // newest table, used from the next note-on.
	std::shared_ptr<fret_plucked::BakeRequest> pending_;   // bake in flight, or null.

	// The sounding note. Holds its own table so a parameter change never pulls the sample from under it.
	std::shared_ptr<const fret_plucked::Table> voiceTable_;
	const fret_plucked::NoteSample* note_{};
	const float* data_{};
	State state_ = State::Idle;
	double pos_ = 0.0;
	float gain_ = 0.0f;
	float level_ = 1.0f;
	float tailMul_ = 1.0f;
	float releaseMul_ = 1.0f;
	int quietCount_ = 0;
	bool inLoop_ = false;
	bool gate_ = false;
	bool voiceActive_ = false;
	bool waitingToStart_ = false; // gate rose before the first table was ready.

public:
	gmpi::ReturnCode open(gmpi::api::IUnknown* phost) override;
	void onSetPins() override;

	template<bool pitchStreaming>
	void subProcess(int sampleFrames);
	void subProcessSilence(int sampleFrames);
	void subProcessWaiting(int sampleFrames);

private:
	fret_plucked::Params currentParams() const;
	void requestTable(const fret_plucked::Params& params);
	void adoptTable(std::shared_ptr<const fret_plucked::Table> table);
	void adoptPendingIfReady();
	void noteOn();
	void startNote();
	void stopVoice();
	void endNote(int blockOffset);
	void chooseSubProcess();
	float t60Multiplier(float seconds) const;

	double stepFor(float pitch) const
	{
		const double hz = 440.0 * exp2(10.0 * pitch - 5.0);
		const double step = hz / note_->rootHz;
		return step < 64.0 ? (step > 0.0 ? step : 0.0) : 64.0;
	}

	// Cubic Hermite; the bake provides one guard sample before and three after each note.
	static float cubic(const float* d, int i, float f)
	{
		const float y0 = d[i - 1];
		const float y1 = d[i + 0];
		const float y2 = d[i + 1];
		const float y3 = d[i + 2];
		return y1 + 0.5f * f * (y2 - y0 + f * (2.0f * y0 - 5.0f * y1 + 4.0f * y2 - y3 + f * (3.0f * (y1 - y2) + y3 - y0)));
	}

	gmpi::AudioInPin pinPitch;
	gmpi::BoolInPin pinGate;
	gmpi::AudioInPin pinVelocity;
	gmpi::IntInPin pinHarmonics;
	gmpi::EnumInPin pinSeries;
	gmpi::FloatInPin pinSpacing;
	gmpi::FloatInPin pinSpectrumTilt;
	gmpi::FloatInPin pinDecayTime;
	gmpi::FloatInPin pinDecayTilt;
	gmpi::FloatInPin pinDetune;
	gmpi::EnumInPin pinDetunePattern;
	gmpi::FloatInPin pinReleaseTime;
	gmpi::IntInPin pinLowestNote;
	gmpi::IntInPin pinHighestNote;
	gmpi::FloatInPin pinVoiceActive;
	gmpi::IntInPin pinRenderMode; // Processor/OfflineRenderMode: 2 = offline render.
	gmpi::AudioOutPin pinSignalOut;
	// Added after the first release: kept last, so pin indices saved by earlier patches still line up.
	gmpi::IntInPin pinUnison;
	gmpi::FloatInPin pinPickNoise;
	gmpi::FloatInPin pinPickTone;
	gmpi::FloatInPin pinBody;
};

template<bool pitchStreaming>
void FretPluckedOsc::subProcess(int sampleFrames)
{
	float* out = getBuffer(pinSignalOut);
	const float* pitch = getBuffer(pinPitch);

	const double loopStart = note_->loopStart;
	const double loopEnd = note_->loopEnd;
	const double loopLength = loopEnd - loopStart;
	constexpr float silence = 3e-5f;   // VCA gain, -90 dB.
	constexpr float quiet = 1e-5f;     // output level, -100 dB.
	constexpr int quietSamples = 2048; // longer than a period of the lowest note.

	double step = stepFor(*pitch);

	// The tail fades at the sample's own rate, so it tracks the playback speed.
	const float tailMul = static_cast<float>(std::pow(static_cast<double>(tailMul_), step));

	for (int s = 0; s < sampleFrames; ++s)
	{
		if constexpr (pitchStreaming)
			step = stepFor(pitch[s]);

		const int i = static_cast<int>(pos_);
		const float sample = cubic(data_, i, static_cast<float>(pos_ - i)) * gain_ * level_;
		out[s] = sample;

		// The sample decays by itself too, so the gain alone overestimates how long the note is audible.
		quietCount_ = std::abs(sample) < quiet ? quietCount_ + 1 : 0;

		pos_ += step;
		if (pos_ >= loopEnd)
			pos_ -= loopLength;

		if (state_ == State::Released)
			gain_ *= releaseMul_;
		else if (inLoop_)
			gain_ *= tailMul;
		else
			inLoop_ = pos_ >= loopStart;

		if (gain_ < silence || (quietCount_ >= quietSamples && (inLoop_ || state_ == State::Released)))
		{
			// A non-streaming output must hold one value from the stop position on.
			for (int r = s; r < sampleFrames; ++r)
				out[r] = 0.0f;
			endNote(s);
			return;
		}
	}
}

#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

// Offline synthesis of the Fret Plucked Oscillator's multisample. No host types, so it can be tested on its own.
namespace fret_plucked
{
enum class Series { All, Odd, Even };
enum class DetunePattern { Alternating, Random };

struct Params
{
	int harmonics = 24;
	Series series = Series::All;
	float spacing = 1.0f;      // partial n sits at (1 + (n-1) * spacing) * f0.
	float spectrumTilt = 1.0f; // initial level of partial n relative to the lowest: (n/lowest)^-tilt.
	float decayTime = 4.0f;    // T60 of the lowest partial, seconds.
	float decayTilt = 1.0f;    // T60 of partial n: decayTime / (n/lowest)^decayTilt.
	float detune = 0.0f;       // average detune per partial, cents. The lowest partial is never detuned.
	DetunePattern detunePattern = DetunePattern::Alternating;
	int lowNote = 24;
	int highNote = 96;

	bool operator==(const Params&) const = default;
};

// Partials stop decaying here, leaving a stationary tail that can loop.
constexpr double freezeSeconds = 2.0;
constexpr double loopSeconds = 0.35;      // minimum; rounded up to whole periods of f0.
constexpr double crossfadeSeconds = 0.08; // clamped to half the loop.
constexpr double maxPartialFraction = 0.45; // of the sample rate.
constexpr float outputPeak = 0.5f;          // 5 V.

struct Partial
{
	int number;       // harmonic number n.
	double ratio;     // frequency relative to f0, spacing and detune applied.
	double amplitude;
	double t60;       // seconds.
};

// The partial set for `p`, before per-note Nyquist culling.
std::vector<Partial> partials(const Params& p);

struct NoteSample
{
	std::size_t offset = 0; // index of sample 0 in Table::samples. One guard sample precedes it.
	int loopStart = 0;      // the loop is [loopStart, loopEnd), followed by 3 guard samples.
	int loopEnd = 0;
	double rootHz = 0.0;    // the f0 actually rendered: loopEnd - loopStart is a whole number of its periods.
};

struct Table
{
	Params params; // as requested, before clamping.
	float sampleRate = 0.0f;
	float tailT60 = 0.0f; // the VCA continues the lowest partial's decay at this rate once in the loop.
	int lowNote = 0;
	std::vector<NoteSample> notes; // lowNote upward, one per semitone.
	std::vector<float> samples;

	const NoteSample& note(int midiNote) const;
	const float* data(const NoteSample& n) const { return samples.data() + n.offset; }
};

// Returns null if `cancelled` is set part way through.
std::shared_ptr<Table> bake(const Params& p, float sampleRate, const std::atomic<bool>* cancelled = nullptr);
}

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
	float detune = 0.0f;       // average detune per partial, Hz, so every partial beats at a similar rate. The lowest is never detuned.
	float stiffness = 0.0f;    // string stiffness B, in units of 1e-4: partial n goes sharp by sqrt(1 + B n^2).
	DetunePattern detunePattern = DetunePattern::Alternating;
	int unison = 0;            // extra copies of every partial, alternately above and below it, each pair quieter.
	float pickNoise = 0.0f;    // pick click level, relative to the string's attack: 1 = as loud.
	float pickTone = 0.5f;     // 0..1 moves the click's band centre from 1 kHz to 8 kHz.
	float body = 0.0f;         // body thump level, relative to the string's attack.
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
constexpr int maxUnison = 8;
constexpr double unisonCents = 6.0;         // detune of the first unison pair; pair k sits at k times this.
constexpr double unisonLevel = 0.7;         // level of the first unison pair; pair k is this to the power k.
constexpr int pickPartials = 48;
constexpr double attackSeconds = 0.05;      // the table is normalised on what follows this, not on the attack peak.
constexpr float limitKnee = 0.5f;           // the attack may exceed 5 V; it is soft-limited above here...
constexpr float limitCeiling = 1.0f;        // ...to stay under 10 V.

struct Partial
{
	int number;            // harmonic number n.
	double ratio;          // frequency relative to f0, spacing and stiffness applied.
	double offsetHz = 0.0; // detune, added after the ratio.
	double scale = 1.0;    // unison copies scale the whole partial, so they are detuned in cents.
	double amplitude;
	double t60;            // seconds.

	double hz(double f0) const { return (ratio * f0 + offsetHz) * scale; }
};

// The partial set for `p`, before per-note Nyquist culling.
std::vector<Partial> partials(const Params& p);

// A short decaying sine at a fixed frequency: one component of the pick click or the body thump.
struct Burst
{
	double hz;
	double amplitude; // relative; the renderer scales each set against the string's attack.
	double t60;       // seconds.
	double phase;     // radians; random, so the set sums to noise rather than a spike.
};

// Different for every note, so neighbouring samples never share a pick, but the same on every bake.
std::vector<Burst> pickBursts(const Params& p, int midiNote);
std::vector<Burst> bodyBursts(int midiNote);

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

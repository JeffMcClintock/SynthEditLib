#include "FretPluckedBake.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <thread>

namespace fret_plucked
{
namespace
{
constexpr double ln1000 = 6.907755278982137; // T60: amplitude falls by 1000.
constexpr double silentAmplitude = 1e-5;     // -100 dB; quieter partials are skipped from there on.
constexpr double twoPi = 6.283185307179586;

// Deterministic per-partial spread, so every bake and every voice agrees.
double randomSpread(int index)
{
	uint64_t x = static_cast<uint64_t>(index) + 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	x ^= x >> 31;
	const double u = static_cast<double>(x >> 11) * (1.0 / 9007199254740992.0); // [0, 1)
	const double magnitude = 0.5 + u;                                           // mean 1.
	return (x & 1) ? magnitude : -magnitude;
}

struct Layout
{
	int loopStart;
	int loopEnd;
	int crossfade;
	double rootHz;
};

Layout layoutFor(int midiNote, double sampleRate)
{
	const double f0 = 440.0 * std::exp2((midiNote - 69) / 12.0);
	const double period = sampleRate / f0;
	const int periods = (std::max)(1, static_cast<int>(std::ceil(loopSeconds * f0)));
	const int loopLength = (std::max)(4, static_cast<int>(std::lround(periods * period)));

	Layout l;
	l.loopStart = static_cast<int>(std::lround(freezeSeconds * sampleRate));
	l.loopEnd = l.loopStart + loopLength;
	l.crossfade = (std::min)(static_cast<int>(std::lround(crossfadeSeconds * sampleRate)), loopLength / 2);
	l.rootHz = periods * sampleRate / loopLength;
	return l;
}

// Renders one note into dest[0 .. loopEnd) and its guard samples.
void renderNote(const std::vector<Partial>& parts, const Layout& l, double sampleRate, float* dest, std::vector<double>& work, std::vector<double>& frozenWork)
{
	const int S = l.loopStart;
	const int E = l.loopEnd;
	const int X = l.crossfade;

	work.assign(E, 0.0);
	frozenWork.assign(X, 0.0); // the tail as it would sound over [S-X, S) with amplitudes already frozen.

	for (const auto& part : parts)
	{
		const double hz = part.ratio * l.rootHz;
		if (hz >= maxPartialFraction * sampleRate)
			continue;

		const double w = twoPi * hz / sampleRate;
		const double c = std::cos(w);
		const double s = std::sin(w);
		const double decay = std::exp(-ln1000 / (part.t60 * sampleRate));
		const double frozenGain = part.amplitude * std::exp(-ln1000 * S / (part.t60 * sampleRate));

		double re = 1.0, im = 0.0; // phase 0: sin starts at zero, so no click at note-on.
		double gain = part.amplitude;
		int t = 0;

		for (; t < S; ++t)
		{
			work[t] += gain * im;
			if (t >= S - X)
				frozenWork[t - (S - X)] += frozenGain * im;

			gain *= decay;
			const double r = re * c - im * s;
			im = re * s + im * c;
			re = r;

			if ((t & 1023) == 1023)
			{
				const double norm = 1.0 / std::sqrt(re * re + im * im);
				re *= norm;
				im *= norm;
				if (gain < silentAmplitude && frozenGain < silentAmplitude)
					break;
			}
		}

		if (t < S) // fell silent before the freeze.
			continue;

		for (; t < E; ++t)
		{
			work[t] += frozenGain * im;
			const double r = re * c - im * s;
			im = re * s + im * c;
			re = r;
		}
	}

	// Crossfade the end of the loop into the frozen tail leading up to the loop start, so the wrap E -> S is continuous.
	for (int i = 0; i < X; ++i)
	{
		const double fade = (i + 1) / static_cast<double>(X + 1);
		auto& y = work[E - X + i];
		y += fade * (frozenWork[i] - y);
	}

	dest[-1] = 0.0f;
	for (int t = 0; t < E; ++t)
		dest[t] = static_cast<float>(work[t]);
	for (int g = 0; g < 3; ++g)
		dest[E + g] = dest[S + g];
}
} // namespace

std::vector<Partial> partials(const Params& p)
{
	const int count = std::clamp(p.harmonics, 1, 256);
	const int step = p.series == Series::All ? 1 : 2;
	const int lowest = p.series == Series::Even ? 2 : 1;

	std::vector<Partial> result;
	result.reserve(count);
	for (int i = 0; i < count; ++i)
	{
		const int n = lowest + i * step;
		const double relative = static_cast<double>(n) / lowest;

		double spread = 0.0;
		if (i > 0)
			spread = p.detunePattern == DetunePattern::Alternating ? ((i & 1) ? -1.0 : 1.0) : randomSpread(i);

		Partial part;
		part.number = n;
		part.ratio = (1.0 + (n - 1) * static_cast<double>(p.spacing)) * std::exp2(p.detune * spread / 1200.0);
		part.amplitude = std::pow(relative, -static_cast<double>(p.spectrumTilt));
		part.t60 = (std::max)(1e-3, p.decayTime / std::pow(relative, static_cast<double>(p.decayTilt)));

		if (part.ratio > 0.0)
			result.push_back(part);
	}
	return result;
}

const NoteSample& Table::note(int midiNote) const
{
	assert(!notes.empty());
	const int i = std::clamp(midiNote - lowNote, 0, static_cast<int>(notes.size()) - 1);
	return notes[i];
}

std::shared_ptr<Table> bake(const Params& requested, float sampleRate, const std::atomic<bool>* cancelled)
{
	auto table = std::make_shared<Table>();
	table->params = requested;

	Params p = requested;
	p.lowNote = std::clamp(p.lowNote, 0, 127);
	p.highNote = std::clamp(p.highNote, p.lowNote, 127);

	table->lowNote = p.lowNote;
	table->sampleRate = sampleRate;
	table->tailT60 = (std::max)(1e-3f, p.decayTime);

	const auto parts = partials(p);
	const int noteCount = p.highNote - p.lowNote + 1;

	std::vector<Layout> layouts(noteCount);
	std::size_t total = 0;
	table->notes.resize(noteCount);
	for (int i = 0; i < noteCount; ++i)
	{
		layouts[i] = layoutFor(p.lowNote + i, sampleRate);
		auto& n = table->notes[i];
		n.offset = total + 1;
		n.loopStart = layouts[i].loopStart;
		n.loopEnd = layouts[i].loopEnd;
		n.rootHz = layouts[i].rootHz;
		total += 1 + layouts[i].loopEnd + 3;
	}
	table->samples.resize(total);

	std::atomic<int> nextNote{0};
	auto worker = [&]
	{
		std::vector<double> work, frozenWork;
		for (int i = nextNote++; i < noteCount; i = nextNote++)
		{
			if (cancelled && cancelled->load(std::memory_order_relaxed))
				return;
			renderNote(parts, layouts[i], sampleRate, table->samples.data() + table->notes[i].offset, work, frozenWork);
		}
	};

	const int threadCount = std::clamp(static_cast<int>(std::thread::hardware_concurrency()) - 1, 1, 8);
	std::vector<std::thread> threads;
	for (int t = 1; t < threadCount; ++t)
		threads.emplace_back(worker);
	worker();
	for (auto& t : threads)
		t.join();

	if (cancelled && cancelled->load(std::memory_order_relaxed))
		return {};

	// One gain for the whole table, so the level relationship between notes survives.
	float peak = 0.0f;
	for (const float s : table->samples)
		peak = (std::max)(peak, std::abs(s));
	if (peak > 0.0f)
	{
		const float gain = outputPeak / peak;
		for (auto& s : table->samples)
			s *= gain;
	}

	return table;
}
} // namespace fret_plucked

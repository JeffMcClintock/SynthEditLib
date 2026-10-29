#include "FretPluckedBake.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <thread>

namespace fret_plucked
{
namespace
{
constexpr double ln1000 = 6.907755278982137; // T60: amplitude falls by 1000.
constexpr double silentAmplitude = 1e-5;     // -100 dB; quieter partials are skipped from there on.
constexpr double twoPi = 6.283185307179586;

uint64_t mix(uint64_t x)
{
	x += 0x9E3779B97F4A7C15ull;
	x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
	x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
	return x ^ (x >> 31);
}

// Deterministic [0, 1) from a key, so every bake and every voice agrees.
double unit(uint64_t key)
{
	return static_cast<double>(mix(key) >> 11) * (1.0 / 9007199254740992.0);
}

uint64_t burstKey(int midiNote, int set, int index, int field)
{
	return (static_cast<uint64_t>(midiNote) << 40) ^ (static_cast<uint64_t>(set) << 32) ^ (static_cast<uint64_t>(index) << 8) ^ static_cast<uint64_t>(field);
}

bool beats(const Params& p)
{
	return p.beating > 0.0f && p.beatRate > 0.0f;
}

// Renders a set of bursts into `out`, scaled so its RMS over the first `window` samples is `rms`.
void renderBursts(const std::vector<Burst>& bursts, double sampleRate, double rms, int window, std::vector<double>& out, std::vector<double>& scratch)
{
	if (bursts.empty() || rms <= 0.0)
		return;

	scratch.assign(out.size(), 0.0);
	int used = 0;
	for (const auto& b : bursts)
	{
		if (b.hz >= maxPartialFraction * sampleRate)
			continue;

		const double w = twoPi * b.hz / sampleRate;
		const double c = std::cos(w);
		const double s = std::sin(w);
		const double decay = std::exp(-ln1000 / (b.t60 * sampleRate));
		double re = std::cos(b.phase), im = std::sin(b.phase);
		double gain = b.amplitude;

		int t = 0;
		for (; t < static_cast<int>(scratch.size()) && gain > silentAmplitude * 1e-3; ++t)
		{
			scratch[t] += gain * im;
			gain *= decay;
			const double r = re * c - im * s;
			im = re * s + im * c;
			re = r;
		}
		used = (std::max)(used, t);
	}

	// Random phases start off zero; a 0.5 ms fade-in keeps that from being a step.
	const int fade = (std::min)(used, static_cast<int>(0.0005 * sampleRate) + 1);
	for (int t = 0; t < fade; ++t)
		scratch[t] *= 0.5 - 0.5 * std::cos(3.141592653589793 * t / fade);

	window = (std::min)(window, static_cast<int>(scratch.size()));
	double sum = 0.0;
	for (int t = 0; t < window; ++t)
		sum += scratch[t] * scratch[t];
	const double measured = std::sqrt(sum / (std::max)(1, window));
	if (measured <= 0.0)
		return;

	const double scale = rms / measured;
	for (int t = 0; t < used; ++t)
		out[t] += scratch[t] * scale;
}

struct Layout
{
	int loopStart;
	int loopEnd;
	int crossfade;
	double rootHz;
};

Layout layoutFor(const Params& p, int midiNote, double sampleRate)
{
	const double f0 = 440.0 * std::exp2((midiNote - 69) / 12.0);
	const double period = sampleRate / f0;
	const int periods = loopPeriods(p, f0);
	const int loopLength = (std::max)(4, static_cast<int>(std::lround(periods * period)));

	Layout l;
	l.loopStart = static_cast<int>(std::lround(freezeSeconds * sampleRate));
	l.loopEnd = l.loopStart + loopLength;
	l.crossfade = (std::min)(static_cast<int>(std::lround(crossfadeSeconds * sampleRate)), loopLength / 2);
	l.rootHz = periods * sampleRate / loopLength;
	return l;
}

struct Scratch
{
	std::vector<double> work, frozenWork, bursts, burstScratch;
};

// Renders one note into dest[0 .. loopEnd) and its guard samples. Returns the string's peak after the attack,
// measured before the pick and body go in, so turning those up never changes the table's level.
double renderNote(const Params& p, int midiNote, const std::vector<Partial>& parts, const Layout& l, double sampleRate, float* dest, Scratch& scratch)
{
	auto& work = scratch.work;
	auto& frozenWork = scratch.frozenWork;

	const int S = l.loopStart;
	const int E = l.loopEnd;
	const int X = l.crossfade;

	work.assign(E, 0.0);
	frozenWork.assign(X, 0.0); // the tail as it would sound over [S-X, S) with amplitudes already frozen.

	auto addSine = [&](double hz, double amplitude, double t60)
	{
		if (hz <= 0.0 || hz >= maxPartialFraction * sampleRate || amplitude <= 0.0)
			return;

		const double w = twoPi * hz / sampleRate;
		const double c = std::cos(w);
		const double s = std::sin(w);
		const double decay = std::exp(-ln1000 / (t60 * sampleRate));
		const double frozenGain = amplitude * std::exp(-ln1000 * S / (t60 * sampleRate));

		double re = 1.0, im = 0.0; // phase 0: sin starts at zero, so no click at note-on, and every twin starts in step.
		double gain = amplitude;
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
			return;

		for (; t < E; ++t)
		{
			work[t] += frozenGain * im;
			const double r = re * c - im * s;
			im = re * s + im * c;
			re = r;
		}
	};

	const double loopLength = (E - S) / sampleRate;
	for (const auto& part : parts)
	{
		const double hz = part.hz(l.rootHz);
		const double share = 1.0 / (1.0 + part.twinLevel);
		addSine(hz, part.amplitude * share, part.t60);
		if (part.twinLevel > 0.0)
			addSine(hz + beatHz(part, hz, loopLength), part.amplitude * part.twinLevel * share, part.t60);
	}

	// Crossfade the end of the loop into the frozen tail leading up to the loop start, so the wrap E -> S is continuous.
	for (int i = 0; i < X; ++i)
	{
		const double fade = (i + 1) / static_cast<double>(X + 1);
		auto& y = work[E - X + i];
		y += fade * (frozenWork[i] - y);
	}

	const int attack = (std::min)(E, static_cast<int>(attackSeconds * sampleRate));
	double sustainPeak = 0.0;
	for (int t = attack; t < E; ++t)
		sustainPeak = (std::max)(sustainPeak, std::abs(work[t]));

	// Pick click and body thump, scaled against the string's own attack. Both are gone long before the loop.
	if (p.pickNoise > 0.0f || p.body > 0.0f)
	{
		double sum = 0.0;
		for (int t = 0; t < attack; ++t)
			sum += work[t] * work[t];
		const double attackRms = std::sqrt(sum / (std::max)(1, attack));

		auto& bursts = scratch.bursts;
		bursts.assign((std::min)(E, static_cast<int>(0.5 * sampleRate)), 0.0);
		renderBursts(pickBursts(p, midiNote), sampleRate, p.pickNoise * attackRms, static_cast<int>(0.01 * sampleRate), bursts, scratch.burstScratch);
		renderBursts(bodyBursts(midiNote), sampleRate, p.body * attackRms, static_cast<int>(0.1 * sampleRate), bursts, scratch.burstScratch);
		for (std::size_t t = 0; t < bursts.size(); ++t)
			work[t] += bursts[t];
	}

	dest[-1] = 0.0f;
	for (int t = 0; t < E; ++t)
		dest[t] = static_cast<float>(work[t]);
	for (int g = 0; g < 3; ++g)
		dest[E + g] = dest[S + g];

	return sustainPeak;
}
} // namespace

std::vector<Partial> partials(const Params& p)
{
	const int count = std::clamp(p.harmonics, 1, 256);
	const int step = p.series == Series::All ? 1 : 2;
	const int lowest = p.series == Series::Even ? 2 : 1;

	// Stiffness sharpens every partial; dividing by the lowest partial's share keeps the note itself in tune.
	const double b = (std::max)(0.0, static_cast<double>(p.stiffness)) * 1e-4;
	const double lowestStretch = std::sqrt(1.0 + b * lowest * lowest);

	const double twinLevel = beats(p) ? maxTwinLevel * std::clamp(static_cast<double>(p.beating), 0.0, 1.0) : 0.0;

	std::vector<Partial> result;
	result.reserve(count * (1 + std::clamp(p.unison, 0, maxUnison)));
	for (int i = 0; i < count; ++i)
	{
		const int n = lowest + i * step;
		const double relative = static_cast<double>(n) / lowest;

		Partial part;
		part.number = n;
		part.ratio = (1.0 + (n - 1) * static_cast<double>(p.spacing)) * std::sqrt(1.0 + b * n * n) / lowestStretch;
		part.amplitude = std::pow(relative, -static_cast<double>(p.spectrumTilt));
		part.t60 = (std::max)(1e-3, p.decayTime / std::pow(relative, static_cast<double>(p.decayTilt)));
		part.twinLevel = twinLevel;
		part.beatsPerHz = p.beatRate / 440.0 * (1.0 + beatSpread * (2.0 * unit(0xBEA7000000000000ull ^ static_cast<uint64_t>(n)) - 1.0));

		if (part.ratio > 0.0)
			result.push_back(part);
	}

	const int unison = std::clamp(p.unison, 0, maxUnison);
	const auto baseCount = result.size();
	for (std::size_t i = 0; i < baseCount; ++i)
	{
		for (int u = 0; u < unison; ++u)
		{
			const int pair = 1 + u / 2;
			const double cents = (u & 1 ? -unisonCents : unisonCents) * pair;

			Partial copy = result[i];
			copy.scale *= std::exp2(cents / 1200.0);
			copy.amplitude *= std::pow(unisonLevel, pair);
			result.push_back(copy);
		}
	}
	return result;
}

int loopPeriods(const Params& p, double f0)
{
	return (std::max)(1, static_cast<int>(std::ceil((beats(p) ? beatLoopSeconds : loopSeconds) * f0)));
}

double beatHz(const Partial& part, double hz, double loopSeconds)
{
	if (part.twinLevel <= 0.0 || part.beatsPerHz <= 0.0)
		return 0.0;
	const double wholeBeats = (std::max)(1.0, std::round(part.beatsPerHz * hz * loopSeconds));
	return wholeBeats / loopSeconds;
}

std::vector<Burst> pickBursts(const Params& p, int midiNote)
{
	std::vector<Burst> result;
	if (p.pickNoise <= 0.0f)
		return result;

	// A plastic pick sounds much the same on every string, so the band is fixed in Hz.
	const double centre = 1000.0 * std::pow(8.0, std::clamp(static_cast<double>(p.pickTone), 0.0, 1.0));
	result.reserve(pickPartials);
	for (int i = 0; i < pickPartials; ++i)
	{
		Burst b;
		b.hz = centre * std::pow(2.5, 2.0 * unit(burstKey(midiNote, 0, i, 0)) - 1.0); // log-uniform, centre/2.5 .. centre*2.5.
		b.t60 = 0.005 * std::pow(8.0, unit(burstKey(midiNote, 0, i, 1)));            // 5 .. 40 ms.
		b.amplitude = 0.5 + unit(burstKey(midiNote, 0, i, 2));
		b.phase = twoPi * unit(burstKey(midiNote, 0, i, 3));
		result.push_back(b);
	}
	return result;
}

std::vector<Burst> bodyBursts(int midiNote)
{
	// Approximate low body modes of an acoustic guitar; they don't follow the note.
	constexpr double modes[] = {100.0, 205.0, 390.0, 580.0};
	std::vector<Burst> result;
	for (int i = 0; i < 4; ++i)
	{
		Burst b;
		b.hz = modes[i] * (1.0 + 0.04 * (2.0 * unit(burstKey(midiNote, 1, i, 0)) - 1.0));
		b.t60 = 0.06 + 0.09 * unit(burstKey(midiNote, 1, i, 1));
		b.amplitude = 1.0 / (1 + i);
		b.phase = twoPi * unit(burstKey(midiNote, 1, i, 3));
		result.push_back(b);
	}
	return result;
}

namespace
{
constexpr uint32_t envelopeBlobVersion = 2; // bump whenever Params changes layout.
}

std::vector<uint8_t> envelopeBlob(const Params& p)
{
	std::vector<uint8_t> blob(sizeof(envelopeBlobVersion) + sizeof(Params));
	std::memcpy(blob.data(), &envelopeBlobVersion, sizeof(envelopeBlobVersion));
	std::memcpy(blob.data() + sizeof(envelopeBlobVersion), &p, sizeof(Params));
	return blob;
}

bool decodeEnvelopeBlob(const std::vector<uint8_t>& blob, Params& p)
{
	uint32_t version = 0;
	if (blob.size() != sizeof(version) + sizeof(Params))
		return false;
	std::memcpy(&version, blob.data(), sizeof(version));
	if (version != envelopeBlobVersion)
		return false;
	std::memcpy(&p, blob.data() + sizeof(version), sizeof(Params));
	return true;
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
		layouts[i] = layoutFor(p, p.lowNote + i, sampleRate);
		auto& n = table->notes[i];
		n.offset = total + 1;
		n.loopStart = layouts[i].loopStart;
		n.loopEnd = layouts[i].loopEnd;
		n.rootHz = layouts[i].rootHz;
		total += 1 + layouts[i].loopEnd + 3;
	}
	table->samples.resize(total);

	std::vector<double> sustainPeaks(noteCount, 0.0);
	std::atomic<int> nextNote{0};
	auto worker = [&]
	{
		Scratch scratch;
		for (int i = nextNote++; i < noteCount; i = nextNote++)
		{
			if (cancelled && cancelled->load(std::memory_order_relaxed))
				return;
			sustainPeaks[i] = renderNote(p, p.lowNote + i, parts, layouts[i], sampleRate, table->samples.data() + table->notes[i].offset, scratch);
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

	// One gain for the whole table, so the level relationship between notes survives. It is set from the string after
	// its attack: in-phase partials and the pick make a brief spike that would otherwise make every note quiet.
	const double peak = *std::max_element(sustainPeaks.begin(), sustainPeaks.end());
	if (peak > 0.0)
	{
		const float gain = static_cast<float>(outputPeak / peak);
		constexpr float range = limitCeiling - limitKnee;
		for (auto& s : table->samples)
		{
			const float y = s * gain;
			const float a = std::abs(y);
			s = a <= limitKnee ? y : std::copysign(limitKnee + range * std::tanh((a - limitKnee) / range), y);
		}
	}

	return table;
}
} // namespace fret_plucked

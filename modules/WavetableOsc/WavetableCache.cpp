#include "WavetableCache.h"
#include "../shared/platform_string.h"

WavetableCache& wavetableCache()
{
	static WavetableCache instance;
	return instance;
}

// Shape indices match the switch in WaveTable::GenerateWavetable.
int builtinWavetableShape(const std::string& name)
{
	if (name == "{Saw}")      return 0;
	if (name == "{Ramp}")     return 1;
	if (name == "{Sine}")     return 2;
	if (name == "{Triangle}") return 3;
	if (name == "{Pulse15}")  return 4;
	if (name == "{Square}")   return 5; // Pulse 50%.
	if (name == "{Pulse85}")  return 6;
	if (name == "{Noise}")    return 7;
	if (name == "{Silence}")  return 9;
	if (name == "{DC}")       return 10;
	return -1;
}

std::shared_ptr<CachedWavetable> WavetableCache::getOrLoad(const std::string& fullUri, float sampleRate)
{
	std::scoped_lock lock{mtx_};

	const CacheKey key{fullUri, sampleRate};
	if (auto it = entries_.find(key); it != entries_.end())
	{
		if (auto alive = it->second.lock())
			return alive;
		// Weak entry expired - rebuild and replace.
	}

	auto entry = std::make_shared<CachedWavetable>();

	// Read the file and plan its slot layout before allocating anything: slot count follows
	// what the file actually holds, and it sizes the raw buffer, the mip layout and the bake.
	const int builtinShape = builtinWavetableShape(fullUri);
	std::vector<float> wave;
	int fileSampleRate = 0;
	WaveTable::SerumMetadata serumMeta;
	WaveTable::FileLayout plan;
	bool haveSamples = false;

	if (builtinShape < 0)
	{
		haveSamples = WaveTable::LoadWaveFile(ToPlatformString(fullUri).c_str(), wave, fileSampleRate, &serumMeta)
			&& !wave.empty();

		if (haveSamples)
			plan = WaveTable::PlanLoad(static_cast<int>(wave.size()), serumMeta);
	}

	// Builtins and the fallback sine have no file to speak for them, so they take the default.
	const int slotCount = haveSamples ? plan.slotCount : WaveTable::DefaultSlotCount;

	// Raw single-cycle buffer. Sized to hold the WaveTable header plus its trailing
	// Wavedata[] (the struct already declares one float at the end, so subtract one).
	const std::size_t rawBytes = sizeof(WaveTable)
		+ static_cast<std::size_t>(slotCount * WaveTable::WavetableFileSampleCount - 1) * sizeof(float);
	entry->rawStorage.resize((rawBytes + sizeof(float) - 1) / sizeof(float));
	entry->raw()->SetSize(slotCount, WaveTable::WavetableFileSampleCount);

	if (builtinShape >= 0)
	{
		entry->raw()->GenerateWavetable(0, 0, slotCount - 1, builtinShape);
	}
	else if (haveSamples)
	{
		entry->raw()->LoadSamples(wave, fileSampleRate, plan);
	}
	else
	{
		// Fallback sine - keeps audio sane rather than silent or harsh.
		entry->raw()->GenerateWavetable(0, 0, slotCount - 1, 2);
	}

	// Bake per-note mip-mapped form. No more morphed in-between slots - the audio loop
	// linearly crossfades adjacent file slots at playback. Mip count is sample-rate-dependent.
	entry->mipInfo.initialize(WaveTable::WavetableFileSampleCount, slotCount, sampleRate);
	entry->bakedStorage.resize(static_cast<std::size_t>(entry->mipInfo.TotalMemoryRequired()) / sizeof(float));
	entry->raw()->CopyAndMipmap2(entry->mipInfo, entry->bakedStorage.data());

	entries_[key] = entry;
	return entry;
}

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

// The slow part of a raw load - file I/O plus slicing - shared by both public entry points.
// Runs with NO lock held; coordination is the caller's job (buildingRaw_).
std::shared_ptr<RawWavetable> WavetableCache::buildRaw(const std::string& fullUri)
{
	auto entry = std::make_shared<RawWavetable>();

	// Read the file and plan its slot layout before allocating anything: slot count follows
	// what the file actually holds, and it sizes the raw buffer - and later, any mip bake.
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
	entry->storage.resize((rawBytes + sizeof(float) - 1) / sizeof(float));
	entry->get()->SetSize(slotCount, WaveTable::WavetableFileSampleCount);

	if (builtinShape >= 0)
	{
		entry->get()->GenerateWavetable(0, 0, slotCount - 1, builtinShape);
	}
	else if (haveSamples)
	{
		entry->get()->LoadSamples(wave, fileSampleRate, plan);
	}
	else
	{
		// Fallback sine - keeps audio sane rather than silent or harsh.
		entry->get()->GenerateWavetable(0, 0, slotCount - 1, 2);
	}

	return entry;
}

std::shared_ptr<RawWavetable> WavetableCache::getOrLoadRaw(const std::string& fullUri)
{
	std::unique_lock lock{mtx_};

	for (;;)
	{
		if (auto it = rawEntries_.find(fullUri); it != rawEntries_.end())
		{
			if (auto alive = it->second.lock())
				return alive;
			// Weak entry expired - rebuild and replace.
		}

		if (!buildingRaw_.count(fullUri))
			break;

		// Another thread is loading this exact file - wait for its result rather than
		// duplicating the work.
		buildDone_.wait(lock);
	}

	// Claim the key and do the slow file I/O outside the lock, so other keys (and the
	// bake level) stay available to other threads meanwhile.
	buildingRaw_.insert(fullUri);
	lock.unlock();

	std::shared_ptr<RawWavetable> entry;
	try
	{
		entry = buildRaw(fullUri);
	}
	catch (...)
	{
		// Release the claim so waiters don't hang, then propagate.
		lock.lock();
		buildingRaw_.erase(fullUri);
		buildDone_.notify_all();
		throw;
	}

	lock.lock();
	rawEntries_[fullUri] = entry;
	buildingRaw_.erase(fullUri);
	buildDone_.notify_all();
	return entry;
}

std::shared_ptr<CachedWavetable> WavetableCache::getOrLoad(const std::string& fullUri, float sampleRate)
{
	const CacheKey key{fullUri, sampleRate};

	std::unique_lock lock{mtx_};

	for (;;)
	{
		if (auto it = entries_.find(key); it != entries_.end())
		{
			if (auto alive = it->second.lock())
				return alive;
			// Weak entry expired - rebuild and replace.
		}

		if (!buildingBaked_.count(key))
			break;

		// Another thread is baking this exact (URI, rate) - wait for its result.
		buildDone_.wait(lock);
	}

	// Claim the key; the load and FFT bake run outside the lock so the GUI's raw-only
	// lookups (and bakes of other keys) never stall behind this one.
	buildingBaked_.insert(key);
	lock.unlock();

	std::shared_ptr<CachedWavetable> entry;
	try
	{
		entry = std::make_shared<CachedWavetable>();
		entry->rawTable = getOrLoadRaw(fullUri); // does its own (brief) locking.

		// Bake per-note mip-mapped form. No more morphed in-between slots - the audio loop
		// linearly crossfades adjacent file slots at playback. Mip count is sample-rate-dependent.
		const int slotCount = entry->raw()->slotCount;
		entry->mipInfo.initialize(WaveTable::WavetableFileSampleCount, slotCount, sampleRate);
		entry->bakedStorage.resize(static_cast<std::size_t>(entry->mipInfo.TotalMemoryRequired()) / sizeof(float));
		entry->raw()->CopyAndMipmap2(entry->mipInfo, entry->bakedStorage.data());
	}
	catch (...)
	{
		// Release the claim so waiters don't hang, then propagate.
		lock.lock();
		buildingBaked_.erase(key);
		buildDone_.notify_all();
		throw;
	}

	lock.lock();
	entries_[key] = entry;
	buildingBaked_.erase(key);
	buildDone_.notify_all();
	return entry;
}

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

std::shared_ptr<CachedWavetable> WavetableCache::getOrLoad(const std::string& fullUri)
{
	std::scoped_lock lock{mtx_};

	if (auto it = entries_.find(fullUri); it != entries_.end())
	{
		if (auto alive = it->second.lock())
			return alive;
		// Weak entry expired - rebuild and replace.
	}

	auto entry = std::make_shared<CachedWavetable>();

	// Raw 64-slot buffer. Sized to hold the WaveTable header plus its trailing
	// Wavedata[] (the struct already declares one float at the end, so subtract one).
	const std::size_t rawBytes = sizeof(WaveTable)
		+ static_cast<std::size_t>(WaveTable::WavetableFileSlotCount * WaveTable::WavetableFileSampleCount - 1) * sizeof(float);
	entry->rawStorage.resize((rawBytes + sizeof(float) - 1) / sizeof(float));
	entry->raw()->SetSize(WaveTable::WavetableFileSlotCount, WaveTable::WavetableFileSampleCount);

	const int builtinShape = builtinWavetableShape(fullUri);
	if (builtinShape >= 0)
	{
		entry->raw()->GenerateWavetable(0, 0, WaveTable::WavetableFileSlotCount - 1, builtinShape);
	}
	else
	{
		const bool loaded = entry->raw()->LoadFile3(ToPlatformString(fullUri).c_str(), true);
		if (!loaded)
		{
			// Fallback sine - keeps audio sane rather than silent or harsh.
			entry->raw()->GenerateWavetable(0, 0, WaveTable::WavetableFileSlotCount - 1, 2);
		}
	}

	// Bake mip-mapped form with morph-interpolated 'ghost' slots between file slots.
	const int morphedSlotCount = WaveTable::MorphedSlotRatio * (WaveTable::WavetableFileSlotCount - 1) + 1;
	entry->mipInfo.initialize(WaveTable::WavetableFileSampleCount, morphedSlotCount);
	entry->bakedStorage.resize(static_cast<std::size_t>(entry->mipInfo.TotalMemoryRequired()) / sizeof(float));
	entry->raw()->CopyAndMipmap2(entry->mipInfo, entry->bakedStorage.data());

	entries_[fullUri] = entry;
	return entry;
}
